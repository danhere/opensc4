// Our paint and hit tests of the game's HTML viewer (docs/html.md, "Ours:
// paint and hit tests"): the nodes' Draw (slot 5), HitTest (slot 7) and the
// links' slots of piece h5, written by hand from the disassembly. Each
// function is a detour on the game's, in groups, off by default:
//
//   ui.html.nodes.paint.blocks  the containers' children drawn when in view,
//                               the containers', blocks', BR's and BLINK's
//                               Draw, HR's two-tone rule
//   ui.html.nodes.paint.text    a text run's Draw: each line piece through
//                               the font's DrawString (slot 51)
//   ui.html.nodes.paint.images  IMG's Draw and its picture's blit, stretch
//                               blit or the window's colour
//   ui.html.nodes.paint.cells   a table cell's background and its children
//                               in a context of its own
//   ui.html.nodes.hit           the hit tests of leaves, containers and
//                               text runs, and whether a node is a link
//   ui.html.nodes.links         a link pressed and let go: its children's
//                               text in the link's active or visited colour
//
// The draw context is the layout context (html_layout.cpp): +0 the window,
// +4 the document, +8 the picture drawn into (the window's +0x68), +0xC the
// client rectangle, the clip of every draw, +0x3C the window's colour (+0x11C,
// made by its slot 108), which fills an image without a picture. The picture's slots, the fonts' slots, the
// stretch blit (0x008D9020) and the rectangle helpers stay the game's; ours
// call them with the same arguments in the same order. Calls between the
// paint's functions go through the game's addresses, so any mix of groups
// works; the nodes are called through their vtables, as the game does.
//
// html_parser::g_useGame (set by the HTML tests) sends every detour to the
// game's function, so one run can paint the same page through the game's
// code and through ours and compare the calls it makes.
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

namespace opensc4::html_paint {
namespace {

using html_parser::g_useGame;

void FirstCall(const char* name, bool& logged)
{
    if (logged)
        return;
    logged = true;
    log::Write("html paint: first %s", name);
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

template <typename T>
T& Static(std::uintptr_t address)
{
    return *reinterpret_cast<T*>(address);
}

struct Rect {
    int32_t left;
    int32_t top;
    int32_t right;
    int32_t bottom;
};
static_assert(sizeof(Rect) == 16);

// ====================================================================================
// The context and the nodes

constexpr std::size_t kWindow = 0x0;   // the HTML window
constexpr std::size_t kDocument = 0x4; // the document
constexpr std::size_t kPicture = 0x8;  // the picture drawn into
constexpr std::size_t kClient = 0xC;   // Rect: the client rectangle, the clip
constexpr std::size_t kMissing = 0x3C; // the window's colour: an image without a picture

// A node: whether it is laid out (+9), its box in the page's coordinates
// (+0xC); a container's children are a vector at +0x1C.
constexpr std::size_t kLaidOut = 0x9;
constexpr std::size_t kBox = 0xC;
constexpr std::size_t kChildren = 0x1C;

// The picture's slots: 6 begin drawing (ret 4, the kind; false draws
// nothing), 7 end it (ret 4), 20 fill (ret 0xC: colour or picture, rect,
// clip), 28 a line (ret 0x18: x1, y1, x2, y2, colour, width), 29 blit
// (ret 0x10: source, its area, rect, clip).
constexpr uint32_t kDrawLines = 0x8004;
constexpr uint32_t kDrawFill = 0x8001;
constexpr uint32_t kDrawPicture = 0x8010;

char* Picture(char* ctx) { return At<char*>(ctx, kPicture); }
Rect* Client(char* ctx) { return &At<Rect>(ctx, kClient); }
bool BeginDraw(char* ctx, uint32_t kind) { return Virtual<uint8_t>(Picture(ctx), 0x18, kind) != 0; }
void EndDraw(char* ctx, uint32_t kind) { Virtual<void>(Picture(ctx), 0x1C, kind); }

bool LaidOut(char* node) { return At<uint8_t>(node, kLaidOut) != 0; }
Rect* Box(char* node) { return &At<Rect>(node, kBox); }
void NodeLayout(char* node, char* ctx) { Virtual<void>(node, 0xC, ctx); }
void NodeDraw(char* node, char* ctx) { Virtual<void>(node, 0x14, ctx); }
bool NodeIsLink(char* node) { return Virtual<uint8_t>(node, 0x18) != 0; }
int32_t NodeHitTest(char* node, char** hit, int32_t x, int32_t y) { return Virtual<int32_t>(node, 0x1C, hit, x, y); }
void NodeSetLinkColour(char* node, uint32_t colour) { Virtual<void>(node, 0x28, colour); }

// The game's helpers, through their addresses.
bool InView(char* ctx, const Rect* r, bool whole)
{
    return CallThis<uint8_t>(addr::HTMLLayout_InView, ctx, r, static_cast<uint8_t>(whole)) != 0;
}
Rect* ToWindowRect(char* ctx, const Rect* in, Rect* out)
{
    return CallThis<Rect*>(addr::HTMLLayout_ToWindowRect, ctx, in, out);
}
void ToWindowPoint(char* ctx, int32_t x, int32_t y, int32_t* outX, int32_t* outY)
{
    CallThis<int32_t>(addr::HTMLLayout_ToWindowPoint, ctx, x, y, outX, outY);
}
bool Contains(const Rect* r, int32_t x, int32_t y)
{
    return CallThis<uint8_t>(addr::cRZRect_Contains, const_cast<Rect*>(r), x, y) != 0;
}
bool ChildInView(char* node, char* ctx) { return CallThis<uint8_t>(addr::HTMLNode_IsInView, node, ctx) != 0; }
void DrawChildren(char* node, char* ctx) { CallThis<void>(addr::HTMLContainerNode_DrawChildren, node, ctx); }
void DrawImage(char* node, const Rect* r, char* ctx) { CallThis<void>(addr::HTMLImageNode_DrawImage, node, r, ctx); }

// ====================================================================================
// Paint: the blocks

// The game's 0x00908F93 (ret 4): whether any of the node's box is in view.
uint8_t IsInView(char* node, char* ctx) { return InView(ctx, Box(node), false) ? 1 : 0; }

// The game's 0x00909263 (ret 4): each child in view drawn (the end of the
// children taken once, before the first).
void DrawChildrenOf(char* node, char* ctx)
{
    char** const last = At<char**>(node, kChildren + 4);
    for (char** child = At<char**>(node, kChildren); child != last; ++child)
        if (ChildInView(*child, ctx))
            NodeDraw(*child, ctx);
}

// The game's 0x00909879 (slot 5 of the containers: A, MAP, SPAN, TR and
// the others with no box of their own to lay out; a jump): their children.
void DrawContainer(char* node, char* ctx) { DrawChildren(node, ctx); }

// The game's 0x0090987E (slot 5 of BLINK): the children, in the second half
// of every second of the game's clock.
void DrawBlink(char* node, char* ctx)
{
    const uint32_t now = reinterpret_cast<uint32_t(__cdecl*)()>(addr::RZ_TimeMilliseconds)();
    if (now % 1000 > 500)
        DrawChildren(node, ctx);
}

// The game's 0x009099C1 (slot 5 of the body, P, the headings, lists and the
// other blocks, and TABLE): laid out first if it is not, then the children.
void DrawBlock(char* node, char* ctx)
{
    if (!LaidOut(node))
        NodeLayout(node, ctx);
    DrawChildren(node, ctx);
}

// The game's 0x00908F85 (slot 5 of BR): nothing drawn, only laid out when it
// is not.
void DrawBreak(char* node, char* ctx)
{
    if (!LaidOut(node))
        NodeLayout(node, ctx);
}

// The game's 0x009093CD (slot 5 of HR): the rule's two colours are made once
// by the window (its slot 108, red, green, blue) and kept in the game's
// statics; a dark top line, and with SIZE above 1 a dark left side and a
// light bottom and right side, in the box in the window's coordinates.
void DrawRule(char* node, char* ctx)
{
    uint32_t& made = Static<uint32_t>(addr::HTMLRuleNode_ColoursMade);
    uint32_t& light = Static<uint32_t>(addr::HTMLRuleNode_Light);
    uint32_t& dark = Static<uint32_t>(addr::HTMLRuleNode_Dark);
    char* const window = At<char*>(ctx, kWindow);
    if (!(made & 1)) {
        made |= 1;
        light = Virtual<uint32_t>(window, 0x1B0, 0xA0u, 0xA0u, 0xA0u);
    }
    if (!(made & 2)) {
        made |= 2;
        dark = Virtual<uint32_t>(window, 0x1B0, 0x40u, 0x40u, 0x40u);
    }
    if (!LaidOut(node))
        NodeLayout(node, ctx);
    if (!BeginDraw(ctx, kDrawLines))
        return;
    Rect r;
    ToWindowRect(ctx, Box(node), &r);
    char* const picture = Picture(ctx);
    Virtual<void>(picture, 0x70, r.left, r.top, r.right, r.top, dark, 1);
    if (At<int32_t>(node, 0x20) > 1) {
        Virtual<void>(picture, 0x70, r.left, r.top, r.left, r.bottom, dark, 1);
        Virtual<void>(picture, 0x70, r.left, r.bottom, r.right, r.bottom, light, 1);
        Virtual<void>(picture, 0x70, r.right, r.top, r.right, r.bottom, light, 1);
    }
    EndDraw(ctx, kDrawLines);
}

// ====================================================================================
// Paint: text

// A text node (vtable 0x00ACE300): the string's body (+0x24), the font
// (+0x34), the colour (+0x38), the line pieces (+0x4C, 24 bytes each: box,
// start, count).
constexpr std::size_t kTextBegin = 0x24;
constexpr std::size_t kTextFont = 0x34;
constexpr std::size_t kTextColour = 0x38;
constexpr std::size_t kTextPieces = 0x4C;
constexpr std::size_t kTextLinkColour = 0x3D; // the colour follows its link's

struct TextPiece {
    Rect box;
    uint32_t from;
    uint32_t count;
};
static_assert(sizeof(TextPiece) == 24);

// The game's 0x00909757 (slot 5): laid out first if it is not, then each
// piece drawn at its corner in the window's coordinates with the font's
// DrawString (slot 51: the picture, x, y, the text and its count, the
// colour, 0x08000000 for the font's second colour, the clip, no pen). The
// string's body is read once, the font for every piece; nothing is asked
// of the picture and nothing tested for view.
void DrawText(char* node, char* ctx)
{
    if (!LaidOut(node))
        NodeLayout(node, ctx);
    const char* const text = At<const char*>(node, kTextBegin);
    TextPiece* const last = At<TextPiece*>(node, kTextPieces + 4);
    for (TextPiece* piece = At<TextPiece*>(node, kTextPieces); piece != last; ++piece) {
        int32_t x;
        int32_t y;
        ToWindowPoint(ctx, piece->box.left, piece->box.top, &x, &y);
        Virtual<uint8_t>(At<char*>(node, kTextFont), 0xCC, Picture(ctx), x, y, text + piece->from, piece->count,
                         At<uint32_t>(node, kTextColour), 0x08000000u, Client(ctx), static_cast<void*>(nullptr));
    }
}

// ====================================================================================
// Paint: images

// An image node (vtable 0x00ACE188): its picture (+0x24, none until it
// arrives), WIDTH and HEIGHT (+0x2C, +0x30). The picture's slots: 9 its
// width, 10 its height, 12 its area (a rectangle).
constexpr std::size_t kImagePicture = 0x24;
constexpr std::size_t kImageWidth = 0x2C;
constexpr std::size_t kImageHeight = 0x30;

// The game's 0x009095E1 (slot 5): laid out first if it is not; drawn when
// any of it is in view, in the window's coordinates.
void DrawImageNode(char* node, char* ctx)
{
    if (!LaidOut(node))
        NodeLayout(node, ctx);
    if (!InView(ctx, Box(node), false))
        return;
    Rect r;
    ToWindowRect(ctx, Box(node), &r);
    DrawImage(node, &r, ctx);
}

// The game's 0x00908FD5 (ret 8): the picture blitted into `r` (slot 29)
// when its size is WIDTH and HEIGHT, else stretched (0x008D9020, which
// answers false when it draws nothing); with no picture, filled with the
// window's colour (context +0x3C, slot 20). Clipped to the client
// rectangle; between the picture's begin and end, when begin answers true.
void DrawImageOf(char* node, const Rect* r, char* ctx)
{
    if (!BeginDraw(ctx, kDrawPicture))
        return;
    char* const source = At<char*>(node, kImagePicture);
    if (source) {
        if (At<int32_t>(node, kImageWidth) == Virtual<int32_t>(source, 0x24)
            && At<int32_t>(node, kImageHeight) == Virtual<int32_t>(source, 0x28)) {
            const Rect* const area = Virtual<const Rect*>(source, 0x30);
            Virtual<void>(Picture(ctx), 0x74, source, area, r, Client(ctx));
        } else {
            const Rect* const area = Virtual<const Rect*>(source, 0x30);
            reinterpret_cast<uint8_t(__cdecl*)(void*, void*, const Rect*, const Rect*, const Rect*)>(
                addr::GZBlit_StretchBlit)(Picture(ctx), source, area, r, Client(ctx));
        }
    } else {
        Virtual<void>(Picture(ctx), 0x50, At<uint32_t>(ctx, kMissing), r, Client(ctx));
    }
    EndDraw(ctx, kDrawPicture);
}

// ====================================================================================
// Paint: table cells

// A cell (vtable 0x00ACE264): its BGCOLOR (+0x34) when it has one (+0x38).
constexpr std::size_t kCellColour = 0x34;
constexpr std::size_t kCellHasColour = 0x38;

// A bottom no line reaches (as the table's cells' margins in the layout).
constexpr int32_t kForever = 0x3FFFFFFF;

// The game's 0x00909F59 (slot 5): the background filled over the box (no
// clip), then the children drawn in a copy of the context whose margins are
// the box's sides (floats that never end), the copy's line flushed after.
// Neither laid out first nor tested for view.
void DrawCell(char* node, char* ctx)
{
    Rect r;
    ToWindowRect(ctx, Box(node), &r);
    if (At<uint8_t>(node, kCellHasColour) && BeginDraw(ctx, kDrawFill)) {
        Virtual<void>(Picture(ctx), 0x50, At<uint32_t>(node, kCellColour), &r, static_cast<Rect*>(nullptr));
        EndDraw(ctx, kDrawFill);
    }
    alignas(4) char copy[0x68];
    CallThis<void*>(addr::HTMLLayout_Copy, copy, ctx);
    CallThis<void>(addr::HTMLLayout_FloatLeft, copy, r.left, kForever);
    CallThis<void>(addr::HTMLLayout_FloatRight, copy, r.right, kForever);
    DrawChildren(node, copy);
    CallThis<void>(addr::HTMLLayout_Flush, copy, static_cast<uint8_t>(1));
    CallThis<void>(addr::HTMLLayout_Destruct, copy);
}

// ====================================================================================
// Hit tests

// The answers of slot 7 (ret 0xC: where the node hit goes, x, y in the
// page's coordinates): 0 not in the node, 1 in it, 2 in a link, which is
// put in `hit`.
constexpr int32_t kMiss = 0;
constexpr int32_t kHit = 1;
constexpr int32_t kLink = 2;

int32_t Hit(char* node, char** hit)
{
    if (!NodeIsLink(node))
        return kHit;
    *hit = node;
    return kLink;
}

// The game's 0x009084D6 (slot 7 of the leaves and IMG): its box.
int32_t HitLeaf(char* node, char** hit, int32_t x, int32_t y)
{
    if (!Contains(Box(node), x, y))
        return kMiss;
    return Hit(node, hit);
}

// The game's 0x009083E7 (slot 7 of the containers): outside the box
// nothing; else the children in order until one answers. A child in a link
// gives that; a child hit without one makes the container the node hit,
// and a link when the container is one (A). The end of the children taken
// once.
int32_t HitContainer(char* node, char** hit, int32_t x, int32_t y)
{
    if (!Contains(Box(node), x, y))
        return kMiss;
    char** const last = At<char**>(node, kChildren + 4);
    for (char** child = At<char**>(node, kChildren); child != last; ++child) {
        const int32_t answer = NodeHitTest(*child, hit, x, y);
        if (answer == kLink)
            return kLink;
        if (answer == kHit) {
            *hit = node;
            return NodeIsLink(node) ? kLink : kHit;
        }
    }
    return kMiss;
}

// The game's 0x009085E0 (slot 7 of a text run): in the box and in one of its
// pieces (the end taken once).
int32_t HitText(char* node, char** hit, int32_t x, int32_t y)
{
    if (!Contains(Box(node), x, y))
        return kMiss;
    TextPiece* const last = At<TextPiece*>(node, kTextPieces + 4);
    for (TextPiece* piece = At<TextPiece*>(node, kTextPieces); piece != last; ++piece)
        if (Contains(&piece->box, x, y))
            return Hit(node, hit);
    return kMiss;
}

// The game's 0x009087C6 (slot 6 of A): a link when it has an HREF (+0x28) or
// the address resolved from it (+0x2C, set only with an HREF); a NAME alone
// (+0x30) is not.
uint32_t IsLink(char* node) { return At<uint32_t>(node, 0x28) != 0 || At<uint32_t>(node, 0x2C) != 0 ? 1 : 0; }

// ====================================================================================
// Links

// The game's 0x00908459 (slot 10 of the containers, ret 4): each child's
// link colour (the end taken once).
void SetLinkColourOfChildren(char* node, uint32_t colour)
{
    char** const last = At<char**>(node, kChildren + 4);
    for (char** child = At<char**>(node, kChildren); child != last; ++child)
        NodeSetLinkColour(*child, colour);
}

// The game's 0x009083D7 (slot 10 of a text run, ret 4): its colour, when it
// is a link's.
void SetLinkColourOfText(char* node, uint32_t colour)
{
    if (At<uint8_t>(node, kTextLinkColour))
        At<uint32_t>(node, kTextColour) = colour;
}

// The game's 0x00908EFD and 0x00908F41 (slots 8 and 9 of A, ret 4: the
// window's context, from its mouse down and up): the link's children in the
// active colour (the body's ALINK, +0x38, else the document's slot 18) or
// the visited one (VLINK, +0x34, slot 19), then the document's slot 17 (the window
// drawn again). With no document the colour is the context's address and
// the document's slot is called on nothing (the game's).
void Recolour(char* node, char* ctx, std::size_t bodyColour, std::size_t documentSlot)
{
    char* const document = At<char*>(ctx, kDocument);
    uint32_t colour;
    if (document) {
        char* const body = Virtual<char*>(document, 0x30);
        colour = body ? At<uint32_t>(body, bodyColour) : Virtual<uint32_t>(document, documentSlot);
    } else {
        colour = static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(ctx));
    }
    NodeSetLinkColour(node, colour);
    Virtual<void>(At<char*>(ctx, kDocument), 0x44);
}

void PressLink(char* node, char* ctx) { Recolour(node, ctx, 0x38, 0x48); }
void ReleaseLink(char* node, char* ctx) { Recolour(node, ctx, 0x34, 0x4C); }

// ====================================================================================
// The detours

#define OPENSC4_PAINT_DETOUR(Ret, Name, Address, Ours, Params, Args)                                           \
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

// Paint.
OPENSC4_PAINT_DETOUR(uint8_t, IsInView, 0x00908F93, IsInView, (, char* ctx), (, ctx))
OPENSC4_PAINT_DETOUR(void, DrawChildren, 0x00909263, DrawChildrenOf, (, char* ctx), (, ctx))
OPENSC4_PAINT_DETOUR(void, DrawContainer, 0x00909879, DrawContainer, (, char* ctx), (, ctx))
OPENSC4_PAINT_DETOUR(void, DrawBlink, 0x0090987E, DrawBlink, (, char* ctx), (, ctx))
OPENSC4_PAINT_DETOUR(void, DrawBlock, 0x009099C1, DrawBlock, (, char* ctx), (, ctx))
OPENSC4_PAINT_DETOUR(void, DrawBreak, 0x00908F85, DrawBreak, (, char* ctx), (, ctx))
OPENSC4_PAINT_DETOUR(void, DrawRule, 0x009093CD, DrawRule, (, char* ctx), (, ctx))
OPENSC4_PAINT_DETOUR(void, DrawText, 0x00909757, DrawText, (, char* ctx), (, ctx))
OPENSC4_PAINT_DETOUR(void, DrawImageNode, 0x009095E1, DrawImageNode, (, char* ctx), (, ctx))
OPENSC4_PAINT_DETOUR(void, DrawImage, 0x00908FD5, DrawImageOf, (, const Rect* r, char* ctx), (, r, ctx))
OPENSC4_PAINT_DETOUR(void, DrawCell, 0x00909F59, DrawCell, (, char* ctx), (, ctx))

// Hit tests and links.
OPENSC4_PAINT_DETOUR(int32_t, HitLeaf, 0x009084D6, HitLeaf, (, char** hit, int32_t x, int32_t y), (, hit, x, y))
OPENSC4_PAINT_DETOUR(int32_t, HitContainer, 0x009083E7, HitContainer, (, char** hit, int32_t x, int32_t y),
                     (, hit, x, y))
OPENSC4_PAINT_DETOUR(int32_t, HitText, 0x009085E0, HitText, (, char** hit, int32_t x, int32_t y), (, hit, x, y))
OPENSC4_PAINT_DETOUR(uint32_t, IsLink, 0x009087C6, IsLink, (), ())
OPENSC4_PAINT_DETOUR(void, SetLinkColourOfChildren, 0x00908459, SetLinkColourOfChildren, (, uint32_t colour),
                     (, colour))
OPENSC4_PAINT_DETOUR(void, SetLinkColourOfText, 0x009083D7, SetLinkColourOfText, (, uint32_t colour), (, colour))
OPENSC4_PAINT_DETOUR(void, PressLink, 0x00908EFD, PressLink, (, char* ctx), (, ctx))
OPENSC4_PAINT_DETOUR(void, ReleaseLink, 0x00908F41, ReleaseLink, (, char* ctx), (, ctx))

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
    log::Write("html paint: %s: %zu functions ours", name, N);
    return true;
}

#define OPENSC4_D(Name, Address) {Address, reinterpret_cast<void*>(&D##Name), Slot(&g_##Name)}

bool InstallBlocks(registry::Mode)
{
    const Detour detours[] = {
        OPENSC4_D(IsInView, addr::HTMLNode_IsInView),
        OPENSC4_D(DrawChildren, addr::HTMLContainerNode_DrawChildren),
        OPENSC4_D(DrawContainer, addr::HTMLContainerNode_Draw),
        OPENSC4_D(DrawBlink, addr::HTMLBlinkNode_Draw),
        OPENSC4_D(DrawBlock, addr::HTMLBlockNode_Draw),
        OPENSC4_D(DrawBreak, addr::HTMLBreakNode_Draw),
        OPENSC4_D(DrawRule, addr::HTMLRuleNode_Draw),
    };
    return InstallAll("paint blocks", detours);
}

bool InstallText(registry::Mode)
{
    const Detour detours[] = {
        OPENSC4_D(DrawText, addr::HTMLTextNode_Draw),
    };
    return InstallAll("paint text", detours);
}

bool InstallImages(registry::Mode)
{
    const Detour detours[] = {
        OPENSC4_D(DrawImageNode, addr::HTMLImageNode_Draw),
        OPENSC4_D(DrawImage, addr::HTMLImageNode_DrawImage),
    };
    return InstallAll("paint images", detours);
}

bool InstallCells(registry::Mode)
{
    const Detour detours[] = {
        OPENSC4_D(DrawCell, addr::HTMLCellNode_Draw),
    };
    return InstallAll("paint cells", detours);
}

bool InstallHit(registry::Mode)
{
    const Detour detours[] = {
        OPENSC4_D(HitLeaf, addr::HTMLNode_HitTest),
        OPENSC4_D(HitContainer, addr::HTMLContainerNode_HitTest),
        OPENSC4_D(HitText, addr::HTMLTextNode_HitTest),
        OPENSC4_D(IsLink, addr::HTMLLinkNode_IsLink),
    };
    return InstallAll("hit tests", detours);
}

bool InstallLinks(registry::Mode)
{
    const Detour detours[] = {
        OPENSC4_D(SetLinkColourOfChildren, addr::HTMLContainerNode_SetLinkColour),
        OPENSC4_D(SetLinkColourOfText, addr::HTMLTextNode_SetLinkColour),
        OPENSC4_D(PressLink, addr::HTMLLinkNode_Press),
        OPENSC4_D(ReleaseLink, addr::HTMLLinkNode_Release),
    };
    return InstallAll("links", detours);
}

const bool g_registered[] = {
    registry::Register({"ui.html.nodes.paint.blocks", "ui/html", registry::Kind::Detour, registry::Phase::Attach,
                        addr::HTMLContainerNode_DrawChildren,
                        "Our HTML blocks' paint: the children in view, the containers, blocks, BR, BLINK and HR's "
                        "rule (7 detours; docs/html.md)",
                        false, InstallBlocks}),
    registry::Register({"ui.html.nodes.paint.text", "ui/html", registry::Kind::Detour, registry::Phase::Attach,
                        addr::HTMLTextNode_Draw,
                        "Our HTML text runs' paint: each line piece through the font (1 detour; docs/html.md)",
                        false, InstallText}),
    registry::Register({"ui.html.nodes.paint.images", "ui/html", registry::Kind::Detour, registry::Phase::Attach,
                        addr::HTMLImageNode_Draw,
                        "Our HTML images' paint: the blit, the stretch blit or the window's colour (2 detours; "
                        "docs/html.md)",
                        false, InstallImages}),
    registry::Register({"ui.html.nodes.paint.cells", "ui/html", registry::Kind::Detour, registry::Phase::Attach,
                        addr::HTMLCellNode_Draw,
                        "Our HTML table cells' paint: the background and the children (1 detour; docs/html.md)",
                        false, InstallCells}),
    registry::Register({"ui.html.nodes.hit", "ui/html", registry::Kind::Detour, registry::Phase::Attach,
                        addr::HTMLContainerNode_HitTest,
                        "Our HTML hit tests: leaves, containers, text runs, and whether a node is a link (4 "
                        "detours; docs/html.md)",
                        false, InstallHit}),
    registry::Register({"ui.html.nodes.links", "ui/html", registry::Kind::Detour, registry::Phase::Attach,
                        addr::HTMLLinkNode_Press,
                        "Our HTML links pressed and let go: the link colours of their text (4 detours; "
                        "docs/html.md)",
                        false, InstallLinks}),
};

} // namespace
} // namespace opensc4::html_paint
