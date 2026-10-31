// Our layout of the game's HTML viewer (docs/html.md, "Ours: layout"):
// the layout context of piece h4 and the layout side of the nodes of h5,
// written by hand from the disassembly. Each function is a detour on the
// game's, in groups, off by default:
//
//   ui.html.layout              the layout context (0x68 bytes, the HTML
//                               window's at +0xE0): its constructor and copy,
//                               the start of a layout, the conversions between
//                               the page and the window, the line of items
//                               waiting (add, measure, flush), new lines,
//                               indents and floats
//   ui.html.nodes.layout.blocks the containers' layout and second pass, the
//                               box's union, and the blocks' Layout (slot 3):
//                               the body, P, CENTER, DIV, headings, DD,
//                               BLOCKQUOTE, lists and their items, BR, HR
//   ui.html.nodes.layout.text   a text run's Layout and its line item (the
//                               line height, how much fits, a line's piece,
//                               whether it is done)
//   ui.html.nodes.layout.images an image's Layout (floats left and right, or
//                               an item of the line) and its line item
//   ui.html.nodes.layout.cells  a table cell's Layout and its box
//
// The STLport containers' code (the context's list and vectors) and the
// rectangle helpers stay the game's; ours call them with the same arguments
// in the same order. Calls between the layout's functions go through the
// game's addresses (and so through the detours), so any mix of groups works.
// The line items are called through their vtables, as the game does.
//
// html_parser::g_useGame (set by the HTML tests, html_parser_autotest.cpp)
// sends every detour to the game's function, so one run can lay the same
// page out through the game's code and through ours and compare the boxes.
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>

namespace opensc4::html_parser {
extern bool g_useGame;
}

namespace opensc4::html_layout {
namespace {

using html_parser::g_useGame;

void FirstCall(const char* name, bool& logged)
{
    if (logged)
        return;
    logged = true;
    log::Write("html layout: first %s", name);
}

#define OPENSC4_FIRST(name)                                                                                    \
    do {                                                                                                       \
        static bool logged_ = false;                                                                           \
        if (!logged_)                                                                                          \
            FirstCall(name, logged_);                                                                          \
    } while (0)

template <typename R, typename... A>
R CallThis(std::uintptr_t fn, void* self, A... a)
{
    return reinterpret_cast<R(__thiscall*)(void*, A...)>(fn)(self, a...);
}

template <typename R, typename... A>
R CallPlain(std::uintptr_t fn, A... a)
{
    return reinterpret_cast<R(__cdecl*)(A...)>(fn)(a...);
}

struct Rect {
    int32_t left;
    int32_t top;
    int32_t right;
    int32_t bottom;
};
static_assert(sizeof(Rect) == 16);

// ====================================================================================
// The layout context

// Its fields. The pen and the margins are in the window's coordinates (the
// page less the scroll position plus the client rectangle's corner, though
// the pen starts at the corner itself, below); the nodes' boxes are kept in
// the page's.
constexpr std::size_t kClient = 0x0C;   // Rect: the window's client rectangle
constexpr std::size_t kView = 0x1C;     // Rect: the part of the page in view
constexpr std::size_t kHeight = 0x2C;   // the line's height, the most of its items'
constexpr std::size_t kAscent = 0x30;   // the line's ascent, the most of its items'
constexpr std::size_t kPenX = 0x34;
constexpr std::size_t kPenY = 0x38;     // the line's top
constexpr std::size_t kSpare = 0x3C;    // zeroed by the constructor, never used here
constexpr std::size_t kLeft = 0x40;     // the left margin
constexpr std::size_t kNextLeft = 0x44; // the left margin of the lines after this one
constexpr std::size_t kRight = 0x48;    // the right margin
constexpr std::size_t kItems = 0x4C;    // STLport list of line items waiting (its header)
constexpr std::size_t kLefts = 0x50;    // vector of LeftEntry: indents and left floats
constexpr std::size_t kRights = 0x5C;   // vector of RightEntry: right floats

// A left margin put by: an indent (floats 0) or an image floating left
// (floats 1, until the line's top reaches bottom), with the margins it
// covers, put back when it ends.
struct LeftEntry {
    uint8_t floats;
    uint8_t pad[3];
    int32_t bottom;
    int32_t left;
    int32_t nextLeft;
};
static_assert(sizeof(LeftEntry) == 16);

struct RightEntry {
    int32_t bottom;
    int32_t right;
};
static_assert(sizeof(RightEntry) == 8);

// A bottom no line reaches: a table cell's margins (0x00909FF2).
constexpr int32_t kForever = 0x3FFFFFFF;

int32_t& I(char* object, std::size_t offset) { return At<int32_t>(object, offset); }

template <typename T>
T*& Begin(char* ctx, std::size_t vector)
{
    return At<T*>(ctx, vector);
}

template <typename T>
T*& End(char* ctx, std::size_t vector)
{
    return At<T*>(ctx, vector + 4);
}

// The list's nodes: next, previous, the item.
struct ListNode {
    ListNode* next;
    ListNode* previous;
    char* item;
};

ListNode* Header(char* ctx) { return At<ListNode*>(ctx, kItems); }

// The line items' slots: 0 how the line is aligned (0 left, 1 centred, 2
// right), 1 the item's height and ascent (ret 8), 2 how much of it fits a
// width (ret 0xC), 3 place it on the line (ret 4; true when it is all
// placed), 4 whether nothing is left of it (true takes it off the line).
uint32_t ItemAlign(char* item) { return Virtual<uint32_t>(item, 0x0); }
void ItemMetrics(char* item, int32_t* ascent, int32_t* height) { Virtual<void>(item, 0x4, ascent, height); }
bool ItemFit(char* item, int32_t width, int32_t* used, bool force)
{
    return Virtual<uint8_t>(item, 0x8, width, used, static_cast<uint8_t>(force)) != 0;
}
bool ItemPlace(char* item, char* ctx) { return Virtual<uint8_t>(item, 0xC, ctx) != 0; }
bool ItemDone(char* item) { return Virtual<uint8_t>(item, 0x10) != 0; }

// The context's functions, through the game's addresses.
void Begin(char* ctx) { CallThis<void>(addr::HTMLLayout_Begin, ctx); }
void Flush(char* ctx, bool all) { CallThis<void>(addr::HTMLLayout_Flush, ctx, static_cast<uint8_t>(all)); }
bool Measure(char* ctx, int32_t* left) { return CallThis<uint8_t>(addr::HTMLLayout_Measure, ctx, left) != 0; }
void NewLine(char* ctx, int32_t advance) { CallThis<void>(addr::HTMLLayout_NewLine, ctx, advance); }
void Indent(char* ctx, int32_t left) { CallThis<void>(addr::HTMLLayout_Indent, ctx, left); }
void Unindent(char* ctx) { CallThis<void>(addr::HTMLLayout_Unindent, ctx); }
void FloatLeft(char* ctx, int32_t left, int32_t bottom)
{
    CallThis<void>(addr::HTMLLayout_FloatLeft, ctx, left, bottom);
}
void FloatRight(char* ctx, int32_t right, int32_t bottom)
{
    CallThis<void>(addr::HTMLLayout_FloatRight, ctx, right, bottom);
}
void HangIndent(char* ctx) { CallThis<void>(addr::HTMLLayout_HangIndent, ctx); }
void AddItem(char* ctx, char* item) { CallThis<void>(addr::HTMLLayout_AddItem, ctx, item); }
bool NoLeftFloat(char* ctx) { return (CallThis<uint32_t>(addr::HTMLLayout_NoLeftFloat, ctx) & 0xFF) != 0; }
bool NoRightFloat(char* ctx) { return (CallThis<uint32_t>(addr::HTMLLayout_NoRightFloat, ctx) & 0xFF) != 0; }
Rect* ToPage(char* ctx, const Rect* in, Rect* out)
{
    return CallThis<Rect*>(addr::HTMLLayout_ToPageRect, ctx, in, out);
}
void CopyContext(char* to, const char* from) { CallThis<void*>(addr::HTMLLayout_Copy, to, from); }
void DestroyContext(char* ctx) { CallThis<void>(addr::HTMLLayout_Destruct, ctx); }

// The game's 0x008FDE3F (ret 0x10): a point of the page in the window's
// coordinates.
int32_t ToWindowPoint(char* ctx, int32_t x, int32_t y, int32_t* outX, int32_t* outY)
{
    *outX = I(ctx, kClient) - I(ctx, kView) + x;
    *outY = I(ctx, kClient + 4) - I(ctx, kView + 4) + y;
    return *outY;
}

// The game's 0x008FDE62 (plain ret): the start of a layout. The margins are
// the client rectangle's sides less the scroll position, the pen's line its
// top less the scroll position, but the pen's x the rectangle's left as it
// is (the game's: it matters only while the scroll position's x is not 0,
// and the first line's flush sets the pen from the margin anyway when the
// line is aligned).
void BeginLayout(char* ctx)
{
    const int32_t top = I(ctx, kClient + 4) - I(ctx, kView + 4);
    const int32_t left = I(ctx, kClient);
    I(ctx, kHeight) = 0;
    I(ctx, kAscent) = 0;
    I(ctx, kPenX) = left;
    I(ctx, kPenY) = top;
    const int32_t scrollX = I(ctx, kView);
    I(ctx, kLeft) = left - scrollX;
    I(ctx, kNextLeft) = left - scrollX;
    I(ctx, kRight) = I(ctx, kClient + 8) - scrollX;
}

// The game's 0x008FDEEC (ret 8): a rectangle of the page in the window's
// coordinates; the result.
Rect* ToWindowRect(char* ctx, const Rect* in, Rect* out)
{
    *out = *in;
    const int32_t dx = I(ctx, kView);
    out->left -= dx;
    out->right -= dx;
    const int32_t dy = I(ctx, kView + 4);
    out->top -= dy;
    out->bottom -= dy;
    out->left += I(ctx, kClient);
    out->right += I(ctx, kClient);
    out->top += I(ctx, kClient + 4);
    out->bottom += I(ctx, kClient + 4);
    return out;
}

// The game's 0x008FDF3C (ret 8): whether a rectangle of the page is in
// view; with `whole`, only when it is all in view or covers the view (its
// part in view is the rectangle itself or the view).
bool InView(char* ctx, const Rect* r, bool whole)
{
    Rect part = *r;
    Rect* const view = &At<Rect>(ctx, kView);
    const uint8_t meets = CallThis<uint8_t>(addr::SC4UI_F79DD60, &part, &part, view);
    if (!meets || !whole)
        return meets != 0;
    if (CallThis<uint32_t>(addr::SC4Rect_EqualsFolded, &part, r) != 0)
        return true;
    return CallThis<uint32_t>(addr::SC4Rect_EqualsFolded, &part, view) != 0;
}

// The game's 0x008FDF8C (ret 8): a rectangle of the window in the page's
// coordinates; the result.
Rect* ToPageRect(char* ctx, const Rect* in, Rect* out)
{
    *out = *in;
    const int32_t dx = I(ctx, kClient);
    out->left -= dx;
    out->right -= dx;
    const int32_t dy = I(ctx, kClient + 4);
    out->top -= dy;
    out->bottom -= dy;
    out->left += I(ctx, kView);
    out->right += I(ctx, kView);
    out->top += I(ctx, kView + 4);
    out->bottom += I(ctx, kView + 4);
    return out;
}

// The game's 0x008FE00E: whether no image floats on the left, the last entry
// being no float or an entry that never ends (a table cell's margin). An
// indent's bottom is not set by the game (below; ours sets it to kForever).
uint32_t NoLeftFloatOf(char* ctx)
{
    LeftEntry* const first = Begin<LeftEntry>(ctx, kLefts);
    LeftEntry* const last = End<LeftEntry>(ctx, kLefts);
    if (first == last)
        return 1;
    return last[-1].bottom >= kForever ? 1 : 0;
}

// The game's 0x008FE029: the same on the right.
uint32_t NoRightFloatOf(char* ctx)
{
    RightEntry* const first = Begin<RightEntry>(ctx, kRights);
    RightEntry* const last = End<RightEntry>(ctx, kRights);
    if (first == last)
        return 1;
    return last[-1].bottom >= kForever ? 1 : 0;
}

// The game's 0x008FE044 (ret 4): measures the items waiting against the
// width between the margins, taking each one's height and ascent into the
// line's; `left` ends as the width left over. True when an item does not
// fit all of itself (the line is full). An item of which nothing fits is
// made to give up something (force) when it is the first and nothing floats
// on either side.
bool MeasureLine(char* ctx, int32_t* left)
{
    *left = I(ctx, kRight) - I(ctx, kLeft);
    ListNode* const header = Header(ctx);
    for (ListNode* node = header->next; node != header; node = node->next) {
        char* const item = node->item;
        int32_t ascent;
        int32_t height;
        ItemMetrics(item, &ascent, &height);
        if (ascent > I(ctx, kAscent))
            I(ctx, kAscent) = ascent;
        if (height > I(ctx, kHeight))
            I(ctx, kHeight) = height;
        int32_t used;
        bool full = ItemFit(item, *left, &used, false);
        *left -= used;
        if (used == 0) {
            if (!full)
                continue;
            if (NoLeftFloat(ctx) && NoRightFloat(ctx) && node == Header(ctx)->next) {
                full = ItemFit(item, *left, &used, true);
                *left -= used;
            }
        }
        if (full)
            return true;
    }
    return false;
}

// The game's 0x008FE116 (ret 4): the next line, `advance` below. The line's
// height and ascent start again, the margin is the next lines' one; floats
// whose bottom the line reached end (the left ones down to the first
// indent, which only Unindent ends).
void NextLine(char* ctx, int32_t advance)
{
    I(ctx, kPenY) += advance;
    I(ctx, kHeight) = 0;
    I(ctx, kAscent) = 0;
    I(ctx, kLeft) = I(ctx, kNextLeft);
    while (Begin<LeftEntry>(ctx, kLefts) != End<LeftEntry>(ctx, kLefts)) {
        LeftEntry* const last = End<LeftEntry>(ctx, kLefts) - 1;
        if (!last->floats || last->bottom > I(ctx, kPenY))
            break;
        I(ctx, kLeft) = last->left;
        I(ctx, kNextLeft) = last->nextLeft;
        --End<LeftEntry>(ctx, kLefts);
    }
    while (Begin<RightEntry>(ctx, kRights) != End<RightEntry>(ctx, kRights)) {
        RightEntry* const last = End<RightEntry>(ctx, kRights) - 1;
        if (last->bottom > I(ctx, kPenY))
            break;
        I(ctx, kRight) = last->right;
        --End<RightEntry>(ctx, kRights);
    }
}

// The game's 0x008FE183 (plain ret): the last left entry ends, an indent or
// a float alike, and its margins come back.
void UnindentOf(char* ctx)
{
    if (Begin<LeftEntry>(ctx, kLefts) == End<LeftEntry>(ctx, kLefts))
        return;
    LeftEntry* const last = End<LeftEntry>(ctx, kLefts) - 1;
    I(ctx, kLeft) = last->left;
    I(ctx, kNextLeft) = last->nextLeft;
    --End<LeftEntry>(ctx, kLefts);
}

// The game's 0x008FE19F (plain ret): the lines after this one start where
// the items waiting end (a numbered item's number, 0x0090907D).
void HangIndentOf(char* ctx)
{
    int32_t left;
    Measure(ctx, &left);
    I(ctx, kNextLeft) = I(ctx, kRight) - left;
}

void EraseNode(char* ctx, ListNode* node)
{
    void* result;
    CallThis<void*>(addr::SC4PtrList_Erase, ctx + kItems, &result, node);
}

// The game's 0x008FE1BB (ret 4): lays out the items waiting, a line at a
// time: the items with nothing left go first; a line is laid out when it
// is full (or, with `all`, when it is not), aligned as its last item says,
// each item placed until the pen reaches the right margin, those placed
// whole taken off; the next line is the line's height below.
void FlushLine(char* ctx, bool all)
{
    while (Header(ctx)->next != Header(ctx)) {
        ListNode* const header = Header(ctx);
        for (ListNode* node = header->next; node != header;) {
            if (!ItemDone(node->item))
                break;
            ListNode* const next = node->next;
            EraseNode(ctx, node);
            node = next;
        }
        if (Header(ctx)->next == Header(ctx))
            return;
        int32_t left;
        if (!Measure(ctx, &left) && !all)
            return;
        const uint32_t align = ItemAlign(Header(ctx)->previous->item);
        if (left < 0)
            left = 0;
        switch (align) {
        case 0:
            I(ctx, kPenX) = I(ctx, kLeft);
            break;
        case 1:
            I(ctx, kPenX) = I(ctx, kLeft) + left / 2;
            break;
        case 2:
            I(ctx, kPenX) = I(ctx, kLeft) + left;
            break;
        default:
            break;
        }
        ListNode* const line = Header(ctx);
        for (ListNode* node = line->next; node != line;) {
            if (ItemPlace(node->item, ctx)) {
                ListNode* const next = node->next;
                EraseNode(ctx, node);
                node = next;
            } else {
                node = node->next;
            }
            if (I(ctx, kPenX) >= I(ctx, kRight))
                break;
        }
        if (I(ctx, kHeight) > 0) {
            NewLine(ctx, I(ctx, kHeight));
            I(ctx, kPenX) = I(ctx, kLeft);
            I(ctx, kHeight) = 0;
            I(ctx, kAscent) = 0;
        }
    }
}

// The game's 0x008FE2C6: the constructor. +4 and the next lines' margin
// (+0x44) are left as they are.
char* ConstructContext(char* ctx)
{
    At<uint32_t>(ctx, 0x0) = 0;
    At<uint32_t>(ctx, 0x8) = 0;
    for (const std::size_t at : {kHeight, kAscent, kPenX, kPenY, kSpare, kLeft, kRight})
        At<uint32_t>(ctx, at) = 0;
    uint8_t allocator;
    CallThis<void*>(addr::cGZDBSegmentPackedFile_ListConstruct, ctx + kItems, &allocator);
    for (std::size_t at = kLefts; at < 0x68; at += 4)
        At<uint32_t>(ctx, at) = 0;
    for (std::size_t at = kClient; at < kHeight; at += 4)
        At<uint32_t>(ctx, at) = 0;
    return ctx;
}

// The game's 0x008FE492 (ret 4): an item for the line. An item aligned
// otherwise than the last one waiting lays out what waits first, all of
// it; then the line is laid out as far as it is full.
void AddItemOf(char* ctx, char* item)
{
    if (Header(ctx)->next != Header(ctx)) {
        const uint32_t align = ItemAlign(item);
        if (ItemAlign(Header(ctx)->previous->item) != align)
            Flush(ctx, true);
    }
    CallThis<void>(addr::SC4PtrList_PushBack, ctx + kItems, &item);
    Flush(ctx, false);
}

// The game's 0x008FE515 (ret 8): an image floating on the left, the margin
// `left` until the line's top reaches `bottom`.
void FloatLeftOf(char* ctx, int32_t left, int32_t bottom)
{
    LeftEntry entry{1, {0, 0, 0}, bottom, I(ctx, kLeft), I(ctx, kNextLeft)};
    CallThis<void>(addr::HTMLLayout_PushLeftEntry, ctx + kLefts, &entry);
    I(ctx, kLeft) = left;
    I(ctx, kNextLeft) = left;
}

// The game's 0x008FE54E (ret 8): the same on the right.
void FloatRightOf(char* ctx, int32_t right, int32_t bottom)
{
    RightEntry entry{bottom, I(ctx, kRight)};
    CallThis<void>(addr::HTMLLayout_PushRightEntry, ctx + kRights, &entry);
    I(ctx, kRight) = right;
}

// The game's 0x008FE579 (ret 4): an indent, the margin `left` until
// Unindent. The game leaves the entry's bottom as its stack has it (the
// frame of the flush before it, 0x008FE1BB, at the same depth); only
// NoLeftFloat reads it, when nothing of a line's first item fits (a word
// longer than the indented line): at kForever or above the item is forced
// as it is outside lists; below it is not, the line gets an empty piece and
// the flush goes on to the next line, and the next. With the groups off, a
// layout test run with a page of such words (lists nested past the page's
// width) ended there; with the detours in between (the game's code through
// them), the stack held a value above kForever and the words were forced.
// Ours puts kForever, so such a word is always forced.
void IndentOf(char* ctx, int32_t left)
{
    LeftEntry entry{0, {0, 0, 0}, kForever, I(ctx, kLeft), I(ctx, kNextLeft)};
    CallThis<void>(addr::HTMLLayout_PushLeftEntry, ctx + kLefts, &entry);
    I(ctx, kLeft) = left;
    I(ctx, kNextLeft) = left;
}

// The game's 0x00909ECD (ret 4): the copy constructor (a table cell lays
// itself out in a copy). Every field, +4 and +0x44 too; the list and the
// vectors through the game's copies.
char* CopyContextOf(char* ctx, const char* from)
{
    char* const source = const_cast<char*>(from);
    for (std::size_t at = 0; at < kItems; at += 4)
        At<uint32_t>(ctx, at) = At<uint32_t>(source, at);
    CallThis<void*>(addr::HTMLLayout_CopyItems, ctx + kItems, source + kItems);
    CallThis<void*>(addr::HTMLLayout_CopyLeftEntries, ctx + kLefts, source + kLefts);
    CallThis<void*>(addr::HTMLLayout_CopyRightEntries, ctx + kRights, source + kRights);
    return ctx;
}

// ====================================================================================
// The nodes

// A node: vtable, kind, whether its box is set (+8), whether it is laid out
// (+9), its box in the page's coordinates (+0xC). A container's children
// are a vector at +0x1C.
constexpr std::size_t kBoxSet = 0x8;
constexpr std::size_t kLaidOut = 0x9;
constexpr std::size_t kBox = 0xC;
constexpr std::size_t kChildren = 0x1C;

Rect& Box(char* node) { return At<Rect>(node, kBox); }

void NodeLayout(char* node, char* ctx) { Virtual<void>(node, 0xC, ctx); }
void NodeFinish(char* node) { Virtual<void>(node, 0x10); }

void AddToBox(char* node, const Rect* r) { CallThis<void>(addr::HTMLNode_AddToBox, node, r); }
void LayOutChildren(char* node, char* ctx) { CallThis<void>(addr::HTMLContainerNode_LayoutChildren, node, ctx); }
void FinishChildren(char* node) { CallThis<void>(addr::HTMLContainerNode_Finish, node); }
void FitCell(char* node, char* ctx) { CallThis<void>(addr::HTMLCellNode_Fit, node, ctx); }

// The game's 0x009084A0 (ret 4): a rectangle taken into the box (the first
// one is the box); an empty one is not.
void AddToBoxOf(char* node, const Rect* r)
{
    if (CallThis<uint32_t>(addr::cRZRect_IsEmpty, const_cast<Rect*>(r)) & 0xFF)
        return;
    if (At<uint8_t>(node, kBoxSet)) {
        CallThis<void*>(addr::SC4Rect_UnionOf, &Box(node), &Box(node), r);
        return;
    }
    Box(node) = *r;
    At<uint8_t>(node, kBoxSet) = 1;
}

// The game's 0x00909299 (ret 4): the children laid out in order, the box
// to be set again.
void LayOutChildrenOf(char* node, char* ctx)
{
    char** const first = At<char**>(node, kChildren);
    char** const last = At<char**>(node, kChildren + 4);
    At<uint8_t>(node, kBoxSet) = 0;
    for (char** child = first; child != last; ++child)
        NodeLayout(*child, ctx);
}

// The game's 0x009092BE (slot 4, plain ret): the second pass, the children
// first, the box their boxes (those laid out) together.
void FinishChildrenOf(char* node)
{
    char** const first = At<char**>(node, kChildren);
    char** const last = At<char**>(node, kChildren + 4);
    for (char** child = first; child != last; ++child) {
        char* const c = *child;
        NodeFinish(c);
        if (At<uint8_t>(c, kLaidOut))
            AddToBox(node, &Box(c));
    }
    At<uint8_t>(node, kLaidOut) = 1;
}

// The game's 0x009092F8 (ret 4): a block (CENTER, DIV, headings, DD) on
// lines of its own.
void LayOutBlock(char* node, char* ctx)
{
    Flush(ctx, true);
    LayOutChildren(node, ctx);
    Flush(ctx, true);
}

// The game's 0x0090931F (ret 4): a rule, 5 pixels in from the pen, SIZE
// (+0x20, at least 1) high and WIDTH (+0x24, not below 0) wide, a
// percentage (+0x28; at most 100) of the width between the margins less 10;
// the next line 10 below it.
void LayOutRule(char* node, char* ctx)
{
    Flush(ctx, true);
    if (I(node, 0x20) < 1)
        I(node, 0x20) = 1;
    if (I(node, 0x24) < 0)
        I(node, 0x24) = 0;
    const bool percent = At<uint8_t>(node, 0x28) != 0;
    if (percent && I(node, 0x24) > 100)
        I(node, 0x24) = 100;
    const int32_t x = I(ctx, kPenX) + 5;
    const int32_t y = I(ctx, kPenY) + 5;
    const int32_t width = percent ? (I(ctx, kRight) - I(ctx, kLeft) - 10) * I(node, 0x24) / 100 : I(node, 0x24);
    const Rect r{x, y, width + x, I(node, 0x20) + y};
    Rect out;
    ToPage(ctx, &r, &out);
    Box(node) = out;
    At<uint8_t>(node, kLaidOut) = 1;
    NewLine(ctx, I(node, 0x20) + 10);
}

// The game's 0x009096A4 (ret 4): a list (UL, OL, and DT), indented by 20.
void LayOutList(char* node, char* ctx)
{
    Flush(ctx, true);
    Indent(ctx, I(ctx, kLeft) + 20);
    I(ctx, kPenX) = I(ctx, kLeft);
    LayOutChildren(node, ctx);
    Flush(ctx, true);
    Unindent(ctx);
}

// The game's 0x009096E6 (ret 4): a list item, its margin kept for the
// lines after a number's hanging indent.
void LayOutItem(char* node, char* ctx)
{
    Flush(ctx, true);
    Indent(ctx, I(ctx, kLeft));
    LayOutChildren(node, ctx);
    Flush(ctx, true);
    Unindent(ctx);
}

// The game's 0x0090971E (ret 4): a paragraph, 6 pixels before and after.
void LayOutParagraph(char* node, char* ctx)
{
    Flush(ctx, true);
    NewLine(ctx, 6);
    LayOutChildren(node, ctx);
    Flush(ctx, true);
    NewLine(ctx, 6);
}

// The game's 0x0090996D (ret 4): BLOCKQUOTE, 12 pixels before and after,
// indented by 20.
void LayOutQuote(char* node, char* ctx)
{
    Flush(ctx, true);
    NewLine(ctx, 12);
    Indent(ctx, I(ctx, kLeft) + 20);
    I(ctx, kPenX) = I(ctx, kLeft);
    LayOutChildren(node, ctx);
    Flush(ctx, true);
    Unindent(ctx);
    NewLine(ctx, 12);
}

// The game's 0x009099E2 (ret 4): the body, from the start of the layout.
void LayOutBody(char* node, char* ctx)
{
    Begin(ctx);
    LayOutChildren(node, ctx);
    Flush(ctx, true);
}

// The game's 0x00909216 (ret 4): a line break; its box the empty one at the
// pen.
void LayOutBreak(char* node, char* ctx)
{
    Flush(ctx, true);
    const Rect r{I(ctx, kPenX), I(ctx, kPenY), I(ctx, kPenX), I(ctx, kPenY)};
    Rect out;
    ToPage(ctx, &r, &out);
    Box(node) = out;
    At<uint8_t>(node, kLaidOut) = 1;
}

// The game's 0x0090907D (ret 4): a numbered item's number (vtable
// 0x00ACD7A0) only sets the hanging indent; its children are not laid out.
void LayOutNumber(char*, char* ctx) { HangIndent(ctx); }

// ====================================================================================
// Text runs

// A text node (vtable 0x00ACE300) with its line item at +0x1C (vtable
// 0x00ACE2EC); the item's fields, from the item: the string object (+4)
// and its body (+8, +0xC), the font (+0x18), preformatted (+0x20), the
// line's alignment (+0x24, the item's slot 0), the text laid out so far
// (+0x28) and how much of the rest fit (+0x2C), the line pieces (+0x30, a
// vector of TextPiece).
constexpr std::size_t kTextItem = 0x1C;
constexpr std::size_t kItemText = 0x4;
constexpr std::size_t kItemBegin = 0x8;
constexpr std::size_t kItemEnd = 0xC;
constexpr std::size_t kItemFont = 0x18;
constexpr std::size_t kItemPre = 0x20;
constexpr std::size_t kItemDone = 0x28;
constexpr std::size_t kItemFits = 0x2C;
constexpr std::size_t kItemPieces = 0x30;

struct TextPiece {
    Rect box;
    uint32_t from;
    uint32_t count;
};
static_assert(sizeof(TextPiece) == 24);

char* NodeOfItem(char* item) { return item - kTextItem; }
char* Font(char* item) { return At<char*>(item, kItemFont); }
const char* TextBegin(char* item) { return At<const char*>(item, kItemBegin); }
uint32_t TextLength(char* item)
{
    return static_cast<uint32_t>(At<const char*>(item, kItemEnd) - At<const char*>(item, kItemBegin));
}

// The game's 0x00909EA1 (ret 4): the pieces of before go, the text starts
// again, and its item goes on the line.
void LayOutText(char* node, char* ctx)
{
    char* const pieces = node + kTextItem + kItemPieces;
    CallThis<void*>(addr::HTMLTextPieces_Erase, pieces, At<void*>(pieces, 0), At<void*>(pieces, 4));
    At<uint32_t>(node, kTextItem + kItemDone) = 0;
    At<uint8_t>(node, kLaidOut) = 0;
    At<uint8_t>(node, kBoxSet) = 0;
    AddItem(ctx, node + kTextItem);
}

// The game's 0x00908641 (slot 1, ret 8): the height is the larger of the
// font's line height (slot 35) and glyph height (slot 37); the ascent the
// font's slot 38, but not more than that height.
void TextMetrics(char* item, int32_t* ascent, int32_t* height)
{
    const int32_t glyphs = Virtual<int32_t>(Font(item), 0x94, 0, 0);
    const int32_t line = Virtual<int32_t>(Font(item), 0x8C);
    *height = line < glyphs ? glyphs : line;
    const int32_t top = Virtual<int32_t>(Font(item), 0x98);
    *ascent = top >= *height ? *height : top;
}

// The game's 0x00909BE1 (slot 2, ret 0xC): how much of the rest fits the
// width (+0x2C), what it uses, and whether some is left over.
// Preformatted text takes up to its next line break and the whole width.
// Otherwise FitString (slot 41) breaks at a space; when nothing fits and
// the item must give something, the count that fits (slot 42) breaks
// anywhere, the width taken one pixel wider until a character fits.
bool TextFit(char* item, int32_t width, int32_t* used, bool force)
{
    const uint32_t from = At<uint32_t>(item, kItemDone);
    const uint32_t length = TextLength(item);
    const char* const text = TextBegin(item) + from;
    const int32_t rest = static_cast<int32_t>(length - from);
    if (At<uint8_t>(item, kItemPre)) {
        uint32_t end = CallThis<uint32_t>(addr::SC4String_FindCharBody, item + kItemBegin, '\n', from);
        end = end >= length ? length : end + 1;
        *used = width;
        At<uint32_t>(item, kItemFits) = end - At<uint32_t>(item, kItemDone);
        return rest > At<int32_t>(item, kItemFits);
    }
    *used = 0;
    int32_t fits = Virtual<int32_t>(Font(item), 0xA4, text, rest, width, 0, 0);
    for (;;) {
        At<int32_t>(item, kItemFits) = fits;
        if (fits != 0) {
            const int32_t measured = Virtual<int32_t>(Font(item), 0xC0, text, At<int32_t>(item, kItemFits));
            *used = measured > width ? width : measured;
            break;
        }
        if (!force)
            break;
        int32_t forcedWidth = 0;
        fits = Virtual<int32_t>(Font(item), 0xA8, text, rest, width, &forcedWidth);
        At<int32_t>(item, kItemFits) = fits;
        if (fits != 0) {
            *used = width < forcedWidth ? width : forcedWidth;
            break;
        }
        ++width;
        fits = Virtual<int32_t>(Font(item), 0xA4, text, rest, width, 0, 0);
    }
    return rest > At<int32_t>(item, kItemFits);
}

// The game's 0x0090A24D (slot 3, ret 4): the part that fits as a piece of
// the line at the pen, the pen moved on by its width with the spaces at its
// end, its box without them; the line's ascent less the font's (slot 38)
// above it, the line's height below the line's top. True when the text is
// all laid out.
bool TextPlace(char* item, char* ctx)
{
    const uint32_t length = TextLength(item);
    const int32_t top = I(ctx, kAscent) - Virtual<int32_t>(Font(item), 0x98);
    const int32_t x = I(ctx, kPenX);
    const char* const text = At<const char*>(item, kItemBegin) + At<uint32_t>(item, kItemDone);
    // The spaces at the end dropped. The character before is found from the
    // string's start (0x0090A8F5), not from the part's, but read from the
    // part's: the same for one-byte text.
    int32_t shown = At<int32_t>(item, kItemFits);
    while (shown > 0) {
        const int32_t before =
            static_cast<int32_t>(CallPlain<uint32_t>(addr::HTMLText_PreviousChar, item + kItemText, shown));
        if (text[before] != ' ')
            break;
        shown = before;
    }
    const int32_t width = Virtual<int32_t>(Font(item), 0xC0, text, shown);
    const int32_t advance = shown == At<int32_t>(item, kItemFits)
                                ? width
                                : Virtual<int32_t>(Font(item), 0xC0, text, At<int32_t>(item, kItemFits));
    I(ctx, kPenX) += advance;
    const Rect r{x, top + I(ctx, kPenY), width + x, I(ctx, kHeight) + I(ctx, kPenY)};
    TextPiece piece;
    ToPage(ctx, &r, &piece.box);
    AddToBox(NodeOfItem(item), &piece.box);
    piece.from = At<uint32_t>(item, kItemDone);
    piece.count = At<uint32_t>(item, kItemFits);
    CallThis<void>(addr::HTMLTextPieces_PushBack, item + kItemPieces, &piece);
    At<uint32_t>(item, kItemDone) += At<uint32_t>(item, kItemFits);
    if (At<uint32_t>(item, kItemDone) >= length) {
        At<uint8_t>(NodeOfItem(item), kLaidOut) = 1;
        return true;
    }
    return false;
}

// The game's 0x00909A11 (slot 4, plain ret): whether nothing but spaces is
// left (they are skipped); never for preformatted text.
bool TextDone(char* item)
{
    const int32_t length = static_cast<int32_t>(TextLength(item));
    if (At<uint8_t>(item, kItemPre))
        return false;
    if (At<int32_t>(item, kItemDone) < length) {
        const char* const text = TextBegin(item);
        int32_t at = At<int32_t>(item, kItemDone);
        while (text[at] == ' ') {
            ++at;
            At<int32_t>(item, kItemDone) = at;
            if (at >= length)
                break;
        }
        if (At<int32_t>(item, kItemDone) < length)
            return false;
    }
    At<uint8_t>(NodeOfItem(item), kLaidOut) = 1;
    return true;
}

// ====================================================================================
// Images

// An image node (vtable 0x00ACE188) with its line item at +0x1C (vtable
// 0x00ACE174): WIDTH, HEIGHT and ALIGN at +0x2C to +0x34 of the node (+0x10
// to +0x18 of the item). ALIGN 0 floats left, 1 right; otherwise the image
// is an item of the line, 2 its top on the line's top, 3 its middle on the
// line's base, others its bottom on it (the item's slot 0, its line
// alignment, is at +0x38 of the node).
constexpr std::size_t kImageItem = 0x1C;
constexpr std::size_t kImageWidth = 0x2C;
constexpr std::size_t kImageHeight = 0x30;
constexpr std::size_t kImageAlign = 0x34;

// The game's 0x009094DF (ret 4): an image with no width or no height is
// laid out as nothing; a float goes on lines of its own at the margin, the
// margin moved past it until the line's top reaches its bottom.
void LayOutImage(char* node, char* ctx)
{
    const int32_t width = I(node, kImageWidth);
    const int32_t height = I(node, kImageHeight);
    if (width == 0 || height == 0) {
        At<uint8_t>(node, kLaidOut) = 1;
        return;
    }
    const int32_t align = I(node, kImageAlign);
    if (align == 0) {
        Flush(ctx, true);
        const int32_t left = I(ctx, kLeft);
        const int32_t top = I(ctx, kPenY);
        const Rect r{left, top, width + left, height + top};
        Rect out;
        ToPage(ctx, &r, &out);
        Box(node) = out;
        At<uint8_t>(node, kLaidOut) = 1;
        I(ctx, kPenX) = r.right;
        FloatLeft(ctx, r.right, r.bottom);
    } else if (align == 1) {
        Flush(ctx, true);
        const int32_t fromRight = I(ctx, kRight) - width;
        const int32_t left = fromRight < I(ctx, kLeft) ? I(ctx, kLeft) : fromRight;
        const int32_t top = I(ctx, kPenY);
        const Rect r{left, top, width + left, height + top};
        Rect out;
        ToPage(ctx, &r, &out);
        Box(node) = out;
        At<uint8_t>(node, kLaidOut) = 1;
        FloatRight(ctx, r.left, r.bottom);
    } else {
        AddItem(ctx, node + kImageItem);
    }
}

// The item's fields, from the item.
constexpr std::size_t kItemWidth = kImageWidth - kImageItem;
constexpr std::size_t kItemHeight = kImageHeight - kImageItem;
constexpr std::size_t kItemAlign = kImageAlign - kImageItem;

// The game's 0x009082CA (slot 1, ret 8): the height; the ascent by ALIGN.
void ImageMetrics(char* item, int32_t* ascent, int32_t* height)
{
    *height = I(item, kItemHeight);
    switch (I(item, kItemAlign)) {
    case 2:
        *ascent = 0;
        break;
    case 3:
        *ascent = static_cast<int32_t>(At<uint32_t>(item, kItemHeight) >> 1);
        break;
    default:
        *ascent = I(item, kItemHeight);
        break;
    }
}

// The game's 0x00908FA5 (slot 2, ret 0xC): all of it or nothing (unless it
// must give something). The width is compared unsigned, so a negative width
// left takes it whole; true unless it filled the width exactly.
bool ImageFit(char* item, int32_t width, int32_t* used, bool force)
{
    const uint32_t own = At<uint32_t>(item, kItemWidth);
    if (static_cast<uint32_t>(width) < own && !force) {
        *used = 0;
        return true;
    }
    *used = static_cast<int32_t>(own);
    return own == static_cast<uint32_t>(width);
}

// The game's 0x0090962B (slot 3, ret 4): placed at the pen by ALIGN, the
// pen moved on by its width.
bool ImagePlace(char* item, char* ctx)
{
    const int32_t height = I(item, kItemHeight);
    int32_t top;
    switch (I(item, kItemAlign)) {
    case 2:
        top = I(ctx, kPenY);
        break;
    case 3:
        top = I(ctx, kPenY) - static_cast<int32_t>(static_cast<uint32_t>(height) >> 1) + I(ctx, kAscent);
        break;
    default:
        top = I(ctx, kPenY) - height + I(ctx, kAscent);
        break;
    }
    const Rect r{I(ctx, kPenX), top, I(item, kItemWidth) + I(ctx, kPenX), height + top};
    Rect out;
    ToPage(ctx, &r, &out);
    char* const node = item - kImageItem;
    Box(node) = out;
    At<uint8_t>(node, kLaidOut) = 1;
    I(ctx, kPenX) += I(item, kItemWidth);
    return true;
}

// ====================================================================================
// Table cells (the table's own layout, 0x0090A448, is still the game's)

// The game's 0x00909089 (ret 4): a cell with nothing at +0x2C gets a box of
// one pixel at the pen if it has none, and at least the width at +0x30.
void FitCellOf(char* node, char* ctx)
{
    if (I(node, 0x2C) != 0)
        return;
    if (!At<uint8_t>(node, kBoxSet)) {
        const Rect r{I(ctx, kPenX), I(ctx, kPenY), I(ctx, kPenX) + 1, I(ctx, kPenY) + 1};
        ToPage(ctx, &r, &Box(node));
        At<uint8_t>(node, kBoxSet) = 1;
    }
    const uint32_t width = static_cast<uint32_t>(Box(node).right - Box(node).left);
    if (width < At<uint32_t>(node, 0x30))
        Box(node).right = Box(node).left + I(node, 0x30);
}

// The game's 0x00909837 (ret 8): a cell laid out in the context it is given,
// its box the rectangle given (the row's, 0x00909FF2).
void LayOutCellIn(char* node, char* ctx, const Rect* r)
{
    LayOutChildren(node, ctx);
    Flush(ctx, true);
    FinishChildren(node);
    At<uint8_t>(node, kBoxSet) = 1;
    ToPage(ctx, r, &Box(node));
    FitCell(node, ctx);
    At<uint8_t>(node, kLaidOut) = 1;
}

// The game's 0x0090A34F (slot 3 of TD, ret 4): a cell laid out in a copy of
// the context; the second pass made here, then the box made again from the
// children laid out.
void LayOutCell(char* node, char* ctx)
{
    alignas(4) char copy[0x68];
    CopyContext(copy, ctx);
    LayOutChildren(node, copy);
    Flush(copy, true);
    FinishChildren(node);
    char** const first = At<char**>(node, kChildren);
    char** const last = At<char**>(node, kChildren + 4);
    At<uint8_t>(node, kLaidOut) = 0;
    At<uint8_t>(node, kBoxSet) = 0;
    Box(node) = Rect{0, 0, 0, 0};
    for (char** child = first; child != last; ++child)
        if (At<uint8_t>(*child, kLaidOut))
            AddToBox(node, &Box(*child));
    FitCell(node, copy);
    At<uint8_t>(node, kLaidOut) = 1;
    DestroyContext(copy);
}

// ====================================================================================
// The detours

#define OPENSC4_LAYOUT_DETOUR(Ret, Name, Address, Ours, Params, Args)                                          \
    using Name##Fn = Ret(__thiscall*)(char* self OPENSC4_UNPAREN Params);                                    \
    Name##Fn g_##Name = nullptr;                                                                               \
    Ret __fastcall D##Name(char* self, void* OPENSC4_UNPAREN Params)                                         \
    {                                                                                                          \
        if (g_useGame)                                                                                         \
            return g_##Name(self OPENSC4_UNPAREN Args);                                                        \
        OPENSC4_FIRST(#Name " " #Address);                                                                     \
        return Ours(self OPENSC4_UNPAREN Args);                                                                \
    }

#define OPENSC4_UNPAREN(...) __VA_ARGS__

// The context.
OPENSC4_LAYOUT_DETOUR(int32_t, ToWindowPoint, 0x008FDE3F, ToWindowPoint,
                      (, int32_t x, int32_t y, int32_t* outX, int32_t* outY), (, x, y, outX, outY))
OPENSC4_LAYOUT_DETOUR(void, Begin, 0x008FDE62, BeginLayout, (), ())
OPENSC4_LAYOUT_DETOUR(Rect*, ToWindowRect, 0x008FDEEC, ToWindowRect, (, const Rect* in, Rect* out), (, in, out))
OPENSC4_LAYOUT_DETOUR(uint8_t, InView, 0x008FDF3C, InView,
                      (, const Rect* r, uint8_t whole), (, r, whole != 0))
OPENSC4_LAYOUT_DETOUR(Rect*, ToPageRect, 0x008FDF8C, ToPageRect, (, const Rect* in, Rect* out), (, in, out))
OPENSC4_LAYOUT_DETOUR(uint32_t, NoLeftFloat, 0x008FE00E, NoLeftFloatOf, (), ())
OPENSC4_LAYOUT_DETOUR(uint32_t, NoRightFloat, 0x008FE029, NoRightFloatOf, (), ())
OPENSC4_LAYOUT_DETOUR(uint8_t, Measure, 0x008FE044, MeasureLine, (, int32_t* left), (, left))
OPENSC4_LAYOUT_DETOUR(void, NewLine, 0x008FE116, NextLine, (, int32_t advance), (, advance))
OPENSC4_LAYOUT_DETOUR(void, Unindent, 0x008FE183, UnindentOf, (), ())
OPENSC4_LAYOUT_DETOUR(void, HangIndent, 0x008FE19F, HangIndentOf, (), ())
OPENSC4_LAYOUT_DETOUR(void, Flush, 0x008FE1BB, FlushLine, (, uint8_t all), (, all != 0))
OPENSC4_LAYOUT_DETOUR(char*, Construct, 0x008FE2C6, ConstructContext, (), ())
OPENSC4_LAYOUT_DETOUR(void, AddItem, 0x008FE492, AddItemOf, (, char* item), (, item))
OPENSC4_LAYOUT_DETOUR(void, FloatLeft, 0x008FE515, FloatLeftOf, (, int32_t left, int32_t bottom), (, left, bottom))
OPENSC4_LAYOUT_DETOUR(void, FloatRight, 0x008FE54E, FloatRightOf, (, int32_t right, int32_t bottom),
                      (, right, bottom))
OPENSC4_LAYOUT_DETOUR(void, Indent, 0x008FE579, IndentOf, (, int32_t left), (, left))
OPENSC4_LAYOUT_DETOUR(char*, Copy, 0x00909ECD, CopyContextOf, (, const char* from), (, from))

// The game's MeasureLine (0x008FE044) calls NoLeftFloat and then
// NoRightFloat with ECX as it was, and pushes EDX (the width left) after
// them: both keep ECX and EDX.
__declspec(naked) void NoLeftFloatThunk()
{
    __asm push ecx
    __asm push edx
    __asm call DNoLeftFloat
    __asm pop edx
    __asm pop ecx
    __asm ret
}

__declspec(naked) void NoRightFloatThunk()
{
    __asm push ecx
    __asm push edx
    __asm call DNoRightFloat
    __asm pop edx
    __asm pop ecx
    __asm ret
}

// The blocks.
OPENSC4_LAYOUT_DETOUR(void, AddToBox, 0x009084A0, AddToBoxOf, (, const Rect* r), (, r))
OPENSC4_LAYOUT_DETOUR(void, LayOutChildren, 0x00909299, LayOutChildrenOf, (, char* ctx), (, ctx))
OPENSC4_LAYOUT_DETOUR(void, FinishChildren, 0x009092BE, FinishChildrenOf, (), ())
OPENSC4_LAYOUT_DETOUR(void, LayOutBlock, 0x009092F8, LayOutBlock, (, char* ctx), (, ctx))
OPENSC4_LAYOUT_DETOUR(void, LayOutRule, 0x0090931F, LayOutRule, (, char* ctx), (, ctx))
OPENSC4_LAYOUT_DETOUR(void, LayOutList, 0x009096A4, LayOutList, (, char* ctx), (, ctx))
OPENSC4_LAYOUT_DETOUR(void, LayOutItem, 0x009096E6, LayOutItem, (, char* ctx), (, ctx))
OPENSC4_LAYOUT_DETOUR(void, LayOutParagraph, 0x0090971E, LayOutParagraph, (, char* ctx), (, ctx))
OPENSC4_LAYOUT_DETOUR(void, LayOutQuote, 0x0090996D, LayOutQuote, (, char* ctx), (, ctx))
OPENSC4_LAYOUT_DETOUR(void, LayOutBody, 0x009099E2, LayOutBody, (, char* ctx), (, ctx))
OPENSC4_LAYOUT_DETOUR(void, LayOutBreak, 0x00909216, LayOutBreak, (, char* ctx), (, ctx))
OPENSC4_LAYOUT_DETOUR(void, LayOutNumber, 0x0090907D, LayOutNumber, (, char* ctx), (, ctx))

// Text.
OPENSC4_LAYOUT_DETOUR(void, LayOutText, 0x00909EA1, LayOutText, (, char* ctx), (, ctx))
OPENSC4_LAYOUT_DETOUR(void, TextMetrics, 0x00908641, TextMetrics, (, int32_t* ascent, int32_t* height),
                      (, ascent, height))
OPENSC4_LAYOUT_DETOUR(uint8_t, TextFit, 0x00909BE1, TextFit,
                      (, int32_t width, int32_t* used, uint8_t force), (, width, used, force != 0))
OPENSC4_LAYOUT_DETOUR(uint8_t, TextPlace, 0x0090A24D, TextPlace, (, char* ctx), (, ctx))
OPENSC4_LAYOUT_DETOUR(uint8_t, TextDone, 0x00909A11, TextDone, (), ())

// Images.
OPENSC4_LAYOUT_DETOUR(void, LayOutImage, 0x009094DF, LayOutImage, (, char* ctx), (, ctx))
OPENSC4_LAYOUT_DETOUR(void, ImageMetrics, 0x009082CA, ImageMetrics, (, int32_t* ascent, int32_t* height),
                      (, ascent, height))
OPENSC4_LAYOUT_DETOUR(uint8_t, ImageFit, 0x00908FA5, ImageFit,
                      (, int32_t width, int32_t* used, uint8_t force), (, width, used, force != 0))
OPENSC4_LAYOUT_DETOUR(uint8_t, ImagePlace, 0x0090962B, ImagePlace, (, char* ctx), (, ctx))

// Cells.
OPENSC4_LAYOUT_DETOUR(void, FitCell, 0x00909089, FitCellOf, (, char* ctx), (, ctx))
OPENSC4_LAYOUT_DETOUR(void, LayOutCellIn, 0x00909837, LayOutCellIn, (, char* ctx, const Rect* r), (, ctx, r))
OPENSC4_LAYOUT_DETOUR(void, LayOutCell, 0x0090A34F, LayOutCell, (, char* ctx), (, ctx))

// ====================================================================================
// Installation

struct Detour {
    std::uintptr_t address;
    void* detour;
    void** original;
};

template <typename Fn>
void** Slot(Fn* fn)
{
    return reinterpret_cast<void**>(fn);
}

template <std::size_t N>
bool InstallAll(const char* name, const Detour (&detours)[N])
{
    if (!game::IsBuild641())
        return false;
    for (const Detour& detour : detours) {
        const addr::Entry* entry = addr_check::Find(detour.address);
        if (!entry || !hook::Install(*entry, detour.detour, detour.original))
            return false;
    }
    log::Write("html layout: %s: %zu functions ours", name, N);
    return true;
}

#define OPENSC4_D(Name, Address) {Address, reinterpret_cast<void*>(&D##Name), Slot(&g_##Name)}

bool InstallLayout(registry::Mode)
{
    const Detour detours[] = {
        OPENSC4_D(ToWindowPoint, addr::HTMLLayout_ToWindowPoint),
        OPENSC4_D(Begin, addr::HTMLLayout_Begin),
        OPENSC4_D(ToWindowRect, addr::HTMLLayout_ToWindowRect),
        OPENSC4_D(InView, addr::HTMLLayout_InView),
        OPENSC4_D(ToPageRect, addr::HTMLLayout_ToPageRect),
        {addr::HTMLLayout_NoLeftFloat, reinterpret_cast<void*>(&NoLeftFloatThunk), Slot(&g_NoLeftFloat)},
        {addr::HTMLLayout_NoRightFloat, reinterpret_cast<void*>(&NoRightFloatThunk), Slot(&g_NoRightFloat)},
        OPENSC4_D(Measure, addr::HTMLLayout_Measure),
        OPENSC4_D(NewLine, addr::HTMLLayout_NewLine),
        OPENSC4_D(Unindent, addr::HTMLLayout_Unindent),
        OPENSC4_D(HangIndent, addr::HTMLLayout_HangIndent),
        OPENSC4_D(Flush, addr::HTMLLayout_Flush),
        OPENSC4_D(Construct, addr::HTMLLayout_Construct),
        OPENSC4_D(AddItem, addr::HTMLLayout_AddItem),
        OPENSC4_D(FloatLeft, addr::HTMLLayout_FloatLeft),
        OPENSC4_D(FloatRight, addr::HTMLLayout_FloatRight),
        OPENSC4_D(Indent, addr::HTMLLayout_Indent),
        OPENSC4_D(Copy, addr::HTMLLayout_Copy),
    };
    return InstallAll("context", detours);
}

bool InstallBlocks(registry::Mode)
{
    const Detour detours[] = {
        OPENSC4_D(AddToBox, addr::HTMLNode_AddToBox),
        OPENSC4_D(LayOutChildren, addr::HTMLContainerNode_LayoutChildren),
        OPENSC4_D(FinishChildren, addr::HTMLContainerNode_Finish),
        OPENSC4_D(LayOutBlock, addr::HTMLBlockNode_Layout),
        OPENSC4_D(LayOutRule, addr::HTMLRuleNode_Layout),
        OPENSC4_D(LayOutList, addr::HTMLListNode_Layout),
        OPENSC4_D(LayOutItem, addr::HTMLItemNode_Layout),
        OPENSC4_D(LayOutParagraph, addr::HTMLParagraphNode_Layout),
        OPENSC4_D(LayOutQuote, addr::HTMLQuoteNode_Layout),
        OPENSC4_D(LayOutBody, addr::HTMLBodyNode_Layout),
        OPENSC4_D(LayOutBreak, addr::HTMLBreakNode_Layout),
        OPENSC4_D(LayOutNumber, addr::HTMLNumberNode_Layout),
    };
    return InstallAll("blocks", detours);
}

bool InstallText(registry::Mode)
{
    const Detour detours[] = {
        OPENSC4_D(LayOutText, addr::HTMLTextNode_Layout),
        OPENSC4_D(TextMetrics, addr::HTMLTextItem_Metrics),
        OPENSC4_D(TextFit, addr::HTMLTextItem_Fit),
        OPENSC4_D(TextPlace, addr::HTMLTextItem_Place),
        OPENSC4_D(TextDone, addr::HTMLTextItem_Done),
    };
    return InstallAll("text", detours);
}

bool InstallImages(registry::Mode)
{
    const Detour detours[] = {
        OPENSC4_D(LayOutImage, addr::HTMLImageNode_Layout),
        OPENSC4_D(ImageMetrics, addr::HTMLImageItem_Metrics),
        OPENSC4_D(ImageFit, addr::HTMLImageItem_Fit),
        OPENSC4_D(ImagePlace, addr::HTMLImageItem_Place),
    };
    return InstallAll("images", detours);
}

bool InstallCells(registry::Mode)
{
    const Detour detours[] = {
        OPENSC4_D(FitCell, addr::HTMLCellNode_Fit),
        OPENSC4_D(LayOutCellIn, addr::HTMLCellNode_LayoutIn),
        OPENSC4_D(LayOutCell, addr::HTMLCellNode_Layout),
    };
    return InstallAll("cells", detours);
}

const bool g_registered[] = {
    registry::Register({"ui.html.layout", "ui/html", registry::Kind::Detour, registry::Phase::Attach,
                        addr::HTMLLayout_Flush,
                        "Our HTML layout context: the line of items, new lines, indents, floats, the conversions "
                        "between the page and the window (18 detours; docs/html.md)",
                        false, InstallLayout}),
    registry::Register({"ui.html.nodes.layout.blocks", "ui/html", registry::Kind::Detour, registry::Phase::Attach,
                        addr::HTMLContainerNode_LayoutChildren,
                        "Our HTML blocks' layout: the containers, the body, P, CENTER, DIV, headings, BLOCKQUOTE, "
                        "lists, BR, HR (12 detours; docs/html.md)",
                        false, InstallBlocks}),
    registry::Register({"ui.html.nodes.layout.text", "ui/html", registry::Kind::Detour, registry::Phase::Attach,
                        addr::HTMLTextItem_Fit,
                        "Our HTML text runs' layout: line breaking and the pieces of each line (5 detours; "
                        "docs/html.md)",
                        false, InstallText}),
    registry::Register({"ui.html.nodes.layout.images", "ui/html", registry::Kind::Detour, registry::Phase::Attach,
                        addr::HTMLImageNode_Layout,
                        "Our HTML images' layout: floats and images in the line (4 detours; docs/html.md)", false,
                        InstallImages}),
    registry::Register({"ui.html.nodes.layout.cells", "ui/html", registry::Kind::Detour, registry::Phase::Attach,
                        addr::HTMLCellNode_Layout,
                        "Our HTML table cells' layout (3 detours; docs/html.md)", false, InstallCells}),
};

} // namespace
} // namespace opensc4::html_layout
