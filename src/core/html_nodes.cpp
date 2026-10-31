// Our node constructors and destructors, colours, character helpers,
// pictures and children of the game's HTML viewer (docs/html.md, "Ours:
// nodes, colours and characters"): the rest of piece h5, written by hand
// from the disassembly.
// Each function is a detour on the game's, in groups, off by default:
//
//   ui.html.nodes.ctors       the base node's constructor, IMG's, LINK's,
//                             META's, the lists', TABLE's, TD's, A's and the
//                             body's
//   ui.html.nodes.ctors.text  a text run's constructor: its entities
//                             decoded, its spaces collapsed (or PRE's line
//                             ends made one), its font found
//   ui.html.nodes.dtors       the destructors of the containers, IMG, LINK,
//                             META, A, the body, TABLE and text runs, and
//                             their deleting destructors (slot 0)
//   ui.html.colors            a colour attribute (the 16 names and
//                             `#rrggbb`), its hex digits, the body's three
//                             link colours
//   ui.html.chars             the character helpers (the next and previous
//                             character, whether one is a space) and the
//                             small string tests and numbers the parser uses
//   ui.html.nodes.pictures    the body's picture and IMG's set, IMG's size
//                             taken from its picture
//   ui.html.nodes.children    the containers' AddChild (slot 1) and TABLE's
//                             WIDTH
//
// The game's strings are STLport's, through the game's own helpers (body
// erase, find, assign and append, the string constructors), and the heap is
// the game's (operator delete, the allocator's Free); ours call them with the
// same arguments in the same order, so the heap sees the same allocations
// and frees. Calls between these functions go through the game's addresses,
// so any mix of groups works.
//
// html_parser::g_useGame (set by the HTML tests) sends every detour to the
// game's function, so one run can build and destroy the same nodes through
// the game's code and through ours and compare them.
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

namespace opensc4::html_nodes {
namespace {

using html_parser::g_useGame;

void FirstCall(const char* name, bool& logged)
{
    if (logged)
        return;
    logged = true;
    log::Write("html nodes: first %s", name);
}

#define OPENSC4_FIRST(name)                                                                                    \
    do {                                                                                                       \
        static bool logged_ = false;                                                                           \
        if (!logged_)                                                                                          \
            FirstCall(name, logged_);                                                                          \
    } while (0)

template <typename Fn>
Fn GameFn(std::uintptr_t address)
{
    return reinterpret_cast<Fn>(address);
}

template <typename R, typename... A>
R CallThis(std::uintptr_t fn, void* self, A... a)
{
    return reinterpret_cast<R(__thiscall*)(void*, A...)>(fn)(self, a...);
}

// ====================================================================================
// The game's strings

// An STLport string's body: begin, end (at a terminating zero), the end of
// its storage.
struct Body {
    char* begin;
    char* end;
    char* storage;
};

// The game's string object (cRZString's layout): a vtable, the body and a
// spare dword. 0x14 bytes.
struct Text {
    uint32_t vtable;
    Body body;
    uint32_t spare;
};
static_assert(sizeof(Text) == 0x14);

uint32_t Length(const Body& body) { return static_cast<uint32_t>(body.end - body.begin); }

constexpr uint32_t kAll = 0xFFFFFFFFu;

void MakeEmpty(Text* text) { CallThis<Text*>(addr::SC4String_SC4String, text); }
void MakeFrom(Text* text, const char* from) { CallThis<Text*>(addr::SC4String_FromText, text, from); }
void Substring(Text* text, const Text* source, uint32_t position, uint32_t count)
{
    CallThis<Text*>(addr::SC4String_Substring, text, source, position, count);
}
void AssignText(Text* text, const Text* source) { CallThis<Text*>(addr::cRZBaseString_Assign, text, source); }
void Erase(Body* body, uint32_t position, uint32_t count)
{
    CallThis<Body*>(addr::SC4String_EraseBody, body, position, count);
}
uint32_t FindChar(Body* body, char c, uint32_t from)
{
    return CallThis<uint32_t>(addr::SC4String_FindCharBody, body, c, from);
}
void AppendPart(Body* body, const Body* source, uint32_t position, uint32_t count)
{
    CallThis<Body*>(addr::SC4StringBody_AppendPart, body, source, position, count);
}
void AppendBody(Body* body, const Body* source) { CallThis<Body*>(addr::SC4String_AppendStd, body, source); }
void PushBack(Body* body, char c) { CallThis<void>(addr::SC4String_PushBack, body, c); }
void Free(void* p) { GameFn<void(__cdecl*)(void*)>(addr::SC4_Free)(p); }

// A string of the game's own going out of scope: the vtable put back, its
// characters freed (the game's inlined destructor).
void Release(Text* text)
{
    text->vtable = static_cast<uint32_t>(addr::SC4String_Vtable);
    if (text->body.begin)
        Free(text->body.begin);
}

// The character helpers, through the game's addresses (ours or the game's).
uint32_t CallNextChar(const Text* text, uint32_t position)
{
    return GameFn<uint32_t(__cdecl*)(const Text*, uint32_t)>(addr::HTMLText_NextChar)(text, position);
}
bool CallIsSpaceAt(const Text* text, uint32_t position)
{
    return GameFn<uint8_t(__cdecl*)(const Text*, uint32_t)>(addr::HTMLText_IsSpaceAt)(text, position) != 0;
}

// ====================================================================================
// The constructors

// The base of every node (0x0090847A, ret 4): vtable 0x00ACD504, the kind
// (+4), two flags (+8, +9: laid out), the box (+0xC) empty.
constexpr uint32_t kNodeVtable = 0x00ACD504;
constexpr uint32_t kContainerVtable = 0x00ACD5E8;

char* ConstructNode(char* self, uint32_t kind)
{
    At<uint32_t>(self, 0x4) = kind;
    At<uint32_t>(self, 0x0) = kNodeVtable;
    At<uint8_t>(self, 0x8) = 0;
    At<uint8_t>(self, 0x9) = 0;
    for (std::size_t at = 0xC; at < 0x1C; at += 4)
        At<uint32_t>(self, at) = 0;
    return self;
}

void CallConstructNode(char* self, uint32_t kind) { CallThis<char*>(addr::HTMLNode_Construct, self, kind); }
void CallConstructContainer(char* self, uint32_t kind)
{
    CallThis<char*>(addr::HTMLContainerNode_Construct, self, kind);
}

// IMG (0x0090850E, 0x38 bytes, kind 3): the image item's two vtables at +0x1C
// and +0x20 (written twice, as the game's inlined base does), the picture
// (+0x24), SRC (+0x28), WIDTH and HEIGHT (+0x2C, +0x30), ALIGN (+0x34, 4:
// the bottom).
char* ConstructImage(char* self)
{
    CallConstructNode(self, 3);
    At<uint32_t>(self, 0x1C) = 0x00ACE15C;
    At<uint32_t>(self, 0x20) = 0x00ACE158;
    for (std::size_t at = 0x24; at <= 0x30; at += 4)
        At<uint32_t>(self, at) = 0;
    At<uint32_t>(self, 0x0) = 0x00ACE188;
    At<uint32_t>(self, 0x1C) = 0x00ACE174;
    At<uint32_t>(self, 0x20) = 0x00ACE170;
    At<uint32_t>(self, 0x34) = 4;
    return self;
}

// LINK (0x0090856D, kind 0xB) and META (0x009085A7, kind 0x21): their three
// strings, LINK's flag at +0x20 set.
char* ConstructHeadLink(char* self)
{
    CallConstructNode(self, 0xB);
    At<uint32_t>(self, 0x1C) = 0;
    At<uint32_t>(self, 0x24) = 0;
    At<uint32_t>(self, 0x0) = 0x00ACE1B4;
    At<uint8_t>(self, 0x20) = 1;
    return self;
}

char* ConstructMeta(char* self)
{
    CallConstructNode(self, 0x21);
    At<uint32_t>(self, 0x1C) = 0;
    At<uint32_t>(self, 0x20) = 0;
    At<uint32_t>(self, 0x24) = 0;
    At<uint32_t>(self, 0x0) = 0x00ACE1E0;
    return self;
}

// UL and OL (0x00908704, ret 8: the kind, 6 or 7, and the depth, +0x28).
char* ConstructList(char* self, uint32_t kind, uint32_t depth)
{
    CallConstructContainer(self, kind);
    At<uint32_t>(self, 0x28) = depth;
    At<uint32_t>(self, 0x0) = 0x00ACE20C;
    return self;
}

// TABLE (0x00908723, 0x94 bytes, kind 0xE): BORDER (+0x28), the flag of
// WIDTH (+0x48), three vectors at +0x4C made by the game's vector
// constructor iterator, the columns and the rows' vectors (+0x70, +0x7C,
// +0x88), CELLPADDING, CELLSPACING and the third spacing 2.
char* ConstructTable(char* self)
{
    CallConstructContainer(self, 0xE);
    At<uint32_t>(self, 0x0) = 0x00ACE238;
    At<uint32_t>(self, 0x28) = 0;
    At<uint8_t>(self, 0x48) = 0;
    GameFn<void(__stdcall*)(void*, uint32_t, uint32_t, std::uintptr_t)>(addr::SC4UI_F41D490)(
        self + 0x4C, 0xC, 3, addr::HTMLTableNode_ClearVector);
    for (std::size_t at = 0x70; at < 0x94; at += 4)
        At<uint32_t>(self, at) = 0;
    At<uint32_t>(self, 0x2C) = 2;
    At<uint32_t>(self, 0x34) = 2;
    At<uint32_t>(self, 0x3C) = 2;
    return self;
}

// TD (0x00908781, kind 0x10): VALIGN (+0x28), its flags (+0x38, +0x39),
// WIDTH's kind (+0x2C, 2: none).
char* ConstructCell(char* self)
{
    CallConstructContainer(self, 0x10);
    At<uint32_t>(self, 0x28) = 0;
    At<uint8_t>(self, 0x38) = 0;
    At<uint8_t>(self, 0x39) = 0;
    At<uint32_t>(self, 0x0) = 0x00ACE264;
    At<uint32_t>(self, 0x2C) = 2;
    return self;
}

// A (0x009087A7, kind 4): HREF, the address resolved from it and NAME
// (+0x28, +0x2C, +0x30), none yet.
char* ConstructLink(char* self)
{
    CallConstructContainer(self, 4);
    At<uint32_t>(self, 0x28) = 0;
    At<uint32_t>(self, 0x2C) = 0;
    At<uint32_t>(self, 0x30) = 0;
    At<uint32_t>(self, 0x0) = 0x00ACE290;
    return self;
}

// The body (0x00908811, ret 0xC: LINK, VLINK and ALINK at +0x30, +0x34,
// +0x38): kind 0, the second vtable at +0x28 (written twice), BACKGROUND
// (+0x2C), its picture (+0x3C), the flag at +0x40.
char* ConstructBody(char* self, uint32_t link, uint32_t visited, uint32_t active)
{
    CallConstructContainer(self, 0);
    At<uint32_t>(self, 0x28) = 0x00ACE158;
    At<uint32_t>(self, 0x2C) = 0;
    At<uint32_t>(self, 0x3C) = 0;
    At<uint32_t>(self, 0x30) = link;
    At<uint32_t>(self, 0x34) = visited;
    At<uint32_t>(self, 0x38) = active;
    At<uint32_t>(self, 0x0) = 0x00ACE2C0;
    At<uint32_t>(self, 0x28) = 0x00ACE2BC;
    At<uint8_t>(self, 0x40) = 0;
    return self;
}

// ====================================================================================
// A text run's constructor

// The document's font record (the top of its font stack) as the text run
// reads it: the family (+4, a string body, faces separated by commas), the
// style flags (+0x14: 1 and 2 ask the document's slots 22 and 23 for a
// family), the size (+0x18), the colour (+0x1C), the height (+0x20), PRE
// (+0x24) and the link's colour flag (+0x25).
constexpr std::size_t kFontFamily = 0x4;
constexpr std::size_t kFontFlags = 0x14;
constexpr std::size_t kFontSize = 0x18;
constexpr std::size_t kFontColour = 0x1C;
constexpr std::size_t kFontHeight = 0x20;
constexpr std::size_t kFontPre = 0x24;
constexpr std::size_t kFontLinkColour = 0x25;

// The game's 0x009089B0 (cdecl: the run's text, PRE): the text again from
// a copy of itself. With PRE each CR, LF or CR LF becomes one LF; else each
// run of spaces becomes one space, the characters taken whole (one or two
// bytes) by the character helpers.
void CollapseText(Text* text, bool pre)
{
    Text copy;
    Substring(&copy, text, 0, kAll);
    const int32_t n = static_cast<int32_t>(Length(copy.body));
    Body* const out = &text->body;
    int32_t i = 0;
    Erase(out, 0, kAll);
    if (pre) {
        int32_t start = 0;
        while (i < n) {
            const char c = copy.body.begin[i];
            if (c == '\r') {
                AppendPart(out, &copy.body, static_cast<uint32_t>(start), static_cast<uint32_t>(i - start));
                PushBack(out, '\n');
                ++i;
                if (i < n && copy.body.begin[i] == '\n')
                    ++i;
                start = i;
            } else if (c == '\n') {
                AppendPart(out, &copy.body, static_cast<uint32_t>(start), static_cast<uint32_t>(i - start));
                PushBack(out, '\n');
                ++i;
                start = i;
            } else {
                ++i;
            }
        }
        AppendPart(out, &copy.body, static_cast<uint32_t>(start), static_cast<uint32_t>(i - start));
    } else {
        while (i < n) {
            if (CallIsSpaceAt(&copy, static_cast<uint32_t>(i))) {
                do
                    i = static_cast<int32_t>(CallNextChar(&copy, static_cast<uint32_t>(i)));
                while (i < n && CallIsSpaceAt(&copy, static_cast<uint32_t>(i)));
                PushBack(out, ' ');
            } else {
                const int32_t start = i;
                do
                    i = static_cast<int32_t>(CallNextChar(&copy, static_cast<uint32_t>(i)));
                while (i < n && !CallIsSpaceAt(&copy, static_cast<uint32_t>(i)));
                AppendPart(out, &copy.body, static_cast<uint32_t>(start), static_cast<uint32_t>(i - start));
            }
        }
    }
    Release(&copy);
}

// The font system's slot 11 (+0x2C): a font of a family, style, size and
// height (the last argument 1).
char* GetFont(char* fonts, Text* family, uint32_t style, char* record)
{
    return Virtual<char*>(fonts, 0x2C, family, style, At<uint32_t>(record, kFontSize),
                          At<uint32_t>(record, kFontHeight), uint32_t{1});
}

// The game's 0x00908B04 (ret 8: the document, the font record; ECX the
// run, not read): the run's font from the font system (held by the game's
// smart pointer for the call). Each face of the family in turn, style 0,
// until one is found; with no family, the document's slot 22 or 23 names
// one (style flag 1 or 2); else, or when no face is found, the font of no
// family in the record's style.
char* FindFont(char* /*self*/, char* doc, char* record)
{
    char* fonts = nullptr;
    CallThis<void*>(addr::SC4Save_F5D4920, &fonts);
    char* found = nullptr;
    Body* const family = &At<Body>(record, kFontFamily);
    bool fallback = false;
    if (family->begin != family->end) {
        Text face;
        MakeEmpty(&face);
        uint32_t from = 0;
        for (;;) {
            uint32_t comma = FindChar(family, ',', from);
            if (comma >= Length(*family))
                comma = Length(*family);
            CallThis<Body*>(addr::SC4StringBody_AssignChars, &face.body, family->begin + from, family->begin + comma);
            found = GetFont(fonts, &face, 0, record);
            if (found)
                break;
            if (comma >= Length(*family)) {
                fallback = true;
                break;
            }
            from = comma + 1;
        }
        Release(&face);
    } else {
        const uint32_t flags = At<uint32_t>(record, kFontFlags);
        if (flags & 1) {
            Text name;
            MakeEmpty(&name);
            // The game hands the slot its own argument's slot (the record's
            // address, which it has already read).
            char* recordSlot = record;
            Virtual<void>(doc, 0x58, &name, &recordSlot);
            found = GetFont(fonts, &name, 0, record);
            Release(&name);
        } else if (flags & 2) {
            Text name;
            MakeEmpty(&name);
            uint32_t out = 0;
            Virtual<void>(doc, 0x5C, &name, &out);
            found = GetFont(fonts, &name, 0, record);
            Release(&name);
        } else {
            fallback = true;
        }
    }
    if (fallback) {
        Text none;
        MakeEmpty(&none);
        found = GetFont(fonts, &none, At<uint32_t>(record, kFontFlags), record);
        Release(&none);
    }
    CallThis<void>(addr::SC4Save_F5D4950, &fonts);
    return found;
}

// The game's 0x00908CA6 (0x58 bytes, ret 0x10: the document, the text,
// the font record, the run's number): kind 1, the text item's vtables at
// +0x1C (written twice), the text (+0x20) with its entities decoded
// (`&#n;` and `&#xn;` as a Latin-1 character, named ones through the
// document's entity map, 0x00901222; an `&` with no `;` after it is kept)
// and collapsed, the font (+0x34, its slot 8 called with -1, 0 and with
// the height, 1), the colour and the two flags (+0x38, +0x3C, +0x3D), the
// number (+0x40), the pieces' vector and progress (+0x44 to +0x54) empty.
char* ConstructText(char* self, char* doc, const Text* source, char* record, uint32_t number)
{
    CallConstructNode(self, 1);
    At<uint32_t>(self, 0x1C) = 0x00ACE15C;
    At<uint32_t>(self, 0x0) = 0x00ACE300;
    At<uint32_t>(self, 0x1C) = 0x00ACE2EC;
    Text* const own = &At<Text>(self, 0x20);
    MakeEmpty(own);
    At<uint32_t>(self, 0x34) = 0;
    At<uint32_t>(self, 0x38) = At<uint32_t>(record, kFontColour);
    At<uint8_t>(self, 0x3C) = At<uint8_t>(record, kFontPre);
    At<uint8_t>(self, 0x3D) = At<uint8_t>(record, kFontLinkColour);
    At<uint32_t>(self, 0x40) = number;
    for (std::size_t at = 0x44; at <= 0x54; at += 4)
        At<uint32_t>(self, at) = 0;
    Text decoded;
    MakeEmpty(&decoded);
    Text rest;
    Substring(&rest, source, 0, kAll);
    for (;;) {
        uint32_t at = FindChar(&rest.body, '&', 0);
        if (at >= Length(rest.body))
            break;
        if (at > 0) {
            AppendPart(&decoded.body, &rest.body, 0, at);
            Erase(&rest.body, 0, at);
        }
        at = FindChar(&rest.body, ';', 0);
        if (at >= Length(rest.body))
            break;
        Text name;
        MakeEmpty(&name);
        Text character;
        CallThis<Text*>(addr::SC4String_ConstructRepeated, &character, '?', uint32_t{1});
        CallThis<Body*>(addr::SC4StringBody_AssignBody, &name.body, &rest.body, uint32_t{1}, at - 1);
        if (GameFn<uint8_t(__cdecl*)(const Text*, char)>(addr::HTMLText_StartsWith)(&name, '#')) {
            using HasPrefixFn = uint8_t(__cdecl*)(const Text*, uint32_t, const char*);
            const bool hex = GameFn<HasPrefixFn>(addr::HTMLText_HasPrefix)(&name, 2, "#x") ||
                             GameFn<HasPrefixFn>(addr::HTMLText_HasPrefix)(&name, 2, "#X");
            using StrtoulFn = uint32_t(__cdecl*)(const char*, char**, int);
            const char code = static_cast<char>(
                hex ? GameFn<StrtoulFn>(addr::SC4_strtoul)(name.body.begin + 2, nullptr, 16)
                    : GameFn<StrtoulFn>(addr::SC4_strtoul)(name.body.begin + 1, nullptr, 10));
            GameFn<void(__cdecl*)(const char*, uint32_t, uint32_t, Text*)>(addr::SC4String_FromWide)(&code, 1, 28591,
                                                                                                 &character);
        } else {
            Text looked;
            Text* const entity = CallThis<Text*>(addr::HTMLEntities_Lookup, doc, &looked, &name);
            AssignText(&character, entity);
            Release(&looked);
        }
        AppendBody(&decoded.body, &character.body);
        Erase(&rest.body, 0, at + 1);
        Release(&character);
        Release(&name);
    }
    AppendBody(&decoded.body, &rest.body);
    AssignText(own, &decoded);
    GameFn<void(__cdecl*)(Text*, uint32_t)>(addr::HTMLTextNode_CollapseText)(own, At<uint8_t>(record, kFontPre));
    char* const font = CallThis<char*>(addr::HTMLTextNode_FindFont, self, doc, record);
    At<char*>(self, 0x34) = font;
    Virtual<void>(font, 0x20, kAll, uint32_t{0});
    Virtual<void>(At<char*>(self, 0x34), 0x20, At<uint32_t>(record, kFontHeight), uint32_t{1});
    if (rest.body.begin)
        Free(rest.body.begin);
    Release(&decoded);
    return self;
}

// ====================================================================================
// The destructors

// A pointer the node owns released: an interface's Release (slot 2), a
// game string's deleting destructor (slot 28, with 1).
void ReleaseInterface(char* p) { Virtual<uint32_t>(p, 0x8); }
void DeleteString(char* p) { Virtual<void*>(p, 0x70, uint32_t{1}); }

// The containers' (0x009086B7): each child's deleting destructor (slot 0,
// with 1), the vector's range erased and its storage freed, the base vtable.
void DestroyContainer(char* self)
{
    char** const end = At<char**>(self, 0x20);
    char*** const children = &At<char**>(self, 0x1C);
    At<uint32_t>(self, 0x0) = kContainerVtable;
    for (char** child = *children; child != end; ++child)
        if (*child)
            Virtual<void*>(*child, 0x0, uint32_t{1});
    CallThis<void>(addr::SC4PtrVector_EraseRange, children, children[0], children[1]);
    if (char** const storage = *children)
        Free(storage);
    At<uint32_t>(self, 0x0) = kNodeVtable;
}

void CallDestroyContainer(char* self) { CallThis<void>(addr::HTMLContainerNode_Destruct, self); }

// IMG's (0x00908289): its vtables put back, the picture released (+0x24)
// and SRC deleted (+0x28), each then cleared.
void DestroyImage(char* self)
{
    At<uint32_t>(self, 0x0) = 0x00ACE188;
    At<uint32_t>(self, 0x1C) = 0x00ACE174;
    At<uint32_t>(self, 0x20) = 0x00ACE170;
    if (char* const picture = At<char*>(self, 0x24)) {
        ReleaseInterface(picture);
        At<uint32_t>(self, 0x24) = 0;
    }
    if (char* const source = At<char*>(self, 0x28)) {
        DeleteString(source);
        At<uint32_t>(self, 0x28) = 0;
    }
    At<uint32_t>(self, 0x0) = kNodeVtable;
}

// LINK's (0x0090834F) and META's (0x0090838A): their three strings deleted
// (not cleared).
void DestroyStrings(char* self, uint32_t vtable, const std::size_t (&fields)[3])
{
    At<uint32_t>(self, 0x0) = vtable;
    for (const std::size_t at : fields)
        if (char* const p = At<char*>(self, at))
            DeleteString(p);
    At<uint32_t>(self, 0x0) = kNodeVtable;
}

void DestroyHeadLink(char* self) { DestroyStrings(self, 0x00ACE1B4, {0x1C, 0x24, 0x28}); }
void DestroyMeta(char* self) { DestroyStrings(self, 0x00ACE1E0, {0x1C, 0x20, 0x24}); }

// A's (0x009087D6): HREF, the address and NAME deleted, then the
// container's.
void DestroyLink(char* self)
{
    At<uint32_t>(self, 0x0) = 0x00ACE290;
    for (const std::size_t at : {0x28u, 0x2Cu, 0x30u})
        if (char* const p = At<char*>(self, at))
            DeleteString(p);
    CallDestroyContainer(self);
}

// The body's (0x00908856): BACKGROUND (+0x2C, operator new's) freed and
// cleared, its picture (+0x3C) released, then the container's.
void DestroyBody(char* self)
{
    At<uint32_t>(self, 0x0) = 0x00ACE2C0;
    At<uint32_t>(self, 0x28) = 0x00ACE2BC;
    if (void* const background = At<void*>(self, 0x2C)) {
        GameFn<void(__cdecl*)(void*)>(addr::SC4_operator_delete)(background);
        At<uint32_t>(self, 0x2C) = 0;
    }
    if (char* const picture = At<char*>(self, 0x3C))
        ReleaseInterface(picture);
    CallDestroyContainer(self);
}

// TABLE's (0x009088B8): the rows' and columns' storage (+0x88, +0x7C,
// +0x70) freed, the three vectors at +0x4C by the game's vector destructor
// iterator, then the container's.
void DestroyTable(char* self)
{
    for (const std::size_t at : {0x88u, 0x7Cu, 0x70u})
        if (void* const p = At<void*>(self, at))
            Free(p);
    GameFn<void(__stdcall*)(void*, uint32_t, uint32_t, std::uintptr_t)>(addr::cSC4Advisors_F41D3C0)(
        self + 0x4C, 0xC, 3, addr::HTMLTableNode_FreeVector);
    CallDestroyContainer(self);
}

// A text run's (0x0090891E): its vtables put back, the font released and
// cleared, the pieces' storage freed, the text destroyed.
void DestroyText(char* self)
{
    At<uint32_t>(self, 0x0) = 0x00ACE300;
    At<uint32_t>(self, 0x1C) = 0x00ACE2EC;
    if (char* const font = At<char*>(self, 0x34)) {
        ReleaseInterface(font);
        At<uint32_t>(self, 0x34) = 0;
    }
    if (void* const pieces = At<void*>(self, 0x4C))
        Free(pieces);
    CallThis<void>(addr::SC4String_Destroy, self + 0x20);
    At<uint32_t>(self, 0x0) = kNodeVtable;
}

// The deleting destructors (slot 0, ret 4: the flags): the destructor
// through the game's address, then operator delete with flag 1.
char* DeleteNode(char* self, uint32_t flags, std::uintptr_t destructor)
{
    CallThis<void>(destructor, self);
    if (flags & 1)
        GameFn<void(__cdecl*)(void*)>(addr::SC4_operator_delete)(self);
    return self;
}

// ====================================================================================
// Colours

// The game's 0x0090A801 (the character in AL, EAX 1 or 0; keeps ECX and
// EDX): a hex digit.
bool IsHexDigit(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

// Calls a function that takes its character in AL (0x0090A801, ours or the
// game's) and answers in EAX: it returns to our caller itself.
__declspec(naked) uint32_t __cdecl CallWithAl(uint32_t /*c*/, std::uintptr_t /*fn*/)
{
    __asm mov eax, dword ptr [esp + 4]
    __asm jmp dword ptr [esp + 8]
}

// A naked function is noreturn to the compiler; call it through a pointer
// it cannot see through.
uint32_t(__cdecl* volatile const g_callWithAl)(uint32_t, std::uintptr_t) = &CallWithAl;

bool CallHexDigit(char c, std::uintptr_t fn)
{
    return (g_callWithAl(static_cast<uint8_t>(c), fn) & 0xFF) != 0;
}

// The 16 colour names, in the game's order, and what each becomes
// (0x00ACE334 to 0x00ACE42C).
struct ColourName {
    const char* name;
    const char* hex;
};
constexpr ColourName kColourNames[] = {
    {"BLACK", "#000000"}, {"SILVER", "#C0C0C0"}, {"GRAY", "#808080"},   {"WHITE", "#FFFFFF"},
    {"MAROON", "#800000"}, {"RED", "#FF0000"},   {"PURPLE", "#800080"}, {"FUCHSIA", "#FF00FF"},
    {"GREEN", "#008000"}, {"LIME", "#00FF00"},   {"OLIVE", "#808000"},  {"YELLOW", "#FFFF00"},
    {"NAVY", "#000080"},  {"BLUE", "#0000FF"},   {"TEAL", "#008080"},   {"AQUA", "#00FFFF"},
};

// The game's 0x0090AA54 (cdecl: the attribute, the colour out): a copy of
// the text, upper case unless it starts with `#`; a colour name becomes
// its `#rrggbb`; a `#` at the front is dropped; six hex digits (each
// through 0x0090A801) give 0xFFrrggbb, anything else leaves the colour as
// it was.
void ParseColour(const Text* text, uint32_t* colour)
{
    Text copy;
    Substring(&copy, text, 0, kAll);
    if (copy.body.end != copy.body.begin && copy.body.begin[0] != '#')
        CallThis<void>(addr::cRZBaseString_MakeUpper, &copy);
    using EqualsFn = uint8_t(__cdecl*)(const Body*, const char*);
    for (const ColourName& entry : kColourNames) {
        if (GameFn<EqualsFn>(addr::SC4String_Equals)(&copy.body, entry.name)) {
            Text hex;
            MakeFrom(&hex, entry.hex);
            AssignText(&copy, &hex);
            if (hex.body.begin)
                Free(hex.body.begin);
            break;
        }
    }
    if (copy.body.end != copy.body.begin && copy.body.begin[0] == '#')
        Erase(&copy.body, 0, 1);
    const char* const at = copy.body.begin;
    bool digits = Length(copy.body) == 6;
    for (int i = 0; digits && i < 6; ++i)
        digits = CallHexDigit(at[i], addr::HTMLColor_IsHexDigit);
    if (digits) {
        const uint32_t value =
            GameFn<uint32_t(__cdecl*)(const char*, char**, int)>(addr::SC4_strtoul)(at, nullptr, 16);
        *colour = 0xFF000000u | (value & 0x00FFFFFFu);
    }
    Release(&copy);
}

// The body's LINK, ALINK and VLINK (0x009081D2, 0x0090820F, 0x0090824C,
// ret 4: the attribute): the colour parsed into 0xFF000000 (black when the
// text is no colour), then made by the framework's colour service (slot
// 34, +0x88) from its red, green and blue. The game pushes the three from
// a frame whose next dword still holds ECX, so the upper bytes of the red
// and green arguments are the node's address; the service reads bytes.
void SetBodyColour(char* self, const Text* text, std::size_t field)
{
    uint32_t colour = 0xFF000000u;
    GameFn<void(__cdecl*)(const Text*, uint32_t*)>(addr::HTMLColor_Parse)(text, &colour);
    char* const colours = GameFn<char*(__cdecl*)()>(addr::SC4_Singleton913C1A)();
    const uint32_t self32 = static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(self));
    const uint32_t red = (colour >> 16) | (self32 << 16);
    const uint32_t green = (colour >> 8) | (self32 << 24);
    At<uint32_t>(self, field) = Virtual<uint32_t>(colours, 0x88, red, green, colour);
}

void SetLink(char* self, const Text* text) { SetBodyColour(self, text, 0x30); }
void SetActiveLink(char* self, const Text* text) { SetBodyColour(self, text, 0x38); }
void SetVisitedLink(char* self, const Text* text) { SetBodyColour(self, text, 0x34); }

// ====================================================================================
// Characters

constexpr uint32_t kDefaultEncoding = 0xFFFFFFFEu; // -2: the text helpers' default (UTF-8)

// The game's 0x0090A8F5 (cdecl: a game string, a position): the position
// of the character before it.
uint32_t PreviousChar(const Text* text, uint32_t position)
{
    return position - GameFn<uint32_t(__cdecl*)(const char*, uint32_t, uint32_t)>(addr::RZText_PreviousCharacterSize)(
                          text->body.begin + position, position, kDefaultEncoding);
}

// The game's 0x0090A947: the position of the character after the one at
// `position` (one byte or more).
uint32_t NextChar(const Text* text, uint32_t position)
{
    const uint32_t left = Length(text->body) - position;
    return position + GameFn<uint32_t(__cdecl*)(const char*, uint32_t, uint32_t)>(addr::RZText_CharacterSize)(
                          text->body.begin + position, left, kDefaultEncoding);
}

// The game's 0x0090A820 (cdecl: the characters, how many are left): whether
// the first is a space. With the game's encoding (RZText's default, always
// 8, UTF-8) a space, tab, form feed, CR or LF; the game's test of a UTF-8
// zero width space (E2 80 8B) compares a sign-extended byte with 0xE2 and
// never holds. Any other encoding converts one character to UTF-16 and
// adds the zero width space (0x200B).
bool IsSpace(const char* text, int32_t left)
{
    if (left <= 0)
        return false;
    if (GameFn<uint32_t(__cdecl*)()>(addr::RZText_DefaultEncoding)() == 8) {
        const char c = text[0];
        return c == ' ' || c == '\t' || c == '\f' || c == '\r' || c == '\n';
    }
    const uint32_t size = GameFn<uint32_t(__cdecl*)(const char*, int32_t, uint32_t)>(addr::RZText_CharacterSize)(
        text, left, kDefaultEncoding);
    uint32_t character = 0;
    uint32_t count = 1;
    GameFn<uint32_t(__cdecl*)(const char*, uint32_t, uint32_t*, uint32_t, uint32_t*, uint32_t)>(addr::RZText_Convert)(
        text, size, &character, kDefaultEncoding, &count, 0x10);
    if (count == 0)
        return false;
    const uint16_t c = static_cast<uint16_t>(character);
    return c == 0x20 || c == 9 || c == 0xC || c == 0x200B || c == 0xD || c == 0xA;
}

// The game's 0x0090A929: whether the character at `position` is a space
// (0x0090A820 on what is left of the string).
bool IsSpaceAt(const Text* text, uint32_t position)
{
    const int32_t left = static_cast<int32_t>(Length(text->body) - position);
    return GameFn<uint8_t(__cdecl*)(const char*, int32_t)>(addr::HTMLText_IsSpace)(text->body.begin + position,
                                                                                   left) != 0;
}

// The game's 0x0090A96C (cdecl: a string, a count, a prefix): whether the
// string is at least `count` long and its first `count` characters are the
// prefix's (a prefix shorter than `count` never matches; a count of 0
// always does).
bool HasPrefix(const Text* text, uint32_t count, const char* prefix)
{
    if (Length(text->body) < count)
        return false;
    const char* at = text->body.begin;
    const char* const end = at + count;
    while (at < end && *prefix && *at == *prefix) {
        ++at;
        ++prefix;
    }
    return at == end;
}

// The game's 0x0090A9AA and 0x0090A9CB (cdecl: a string, a character):
// whether it starts, or ends, with the character.
bool StartsWith(const Text* text, char c) { return Length(text->body) >= 1 && text->body.begin[0] == c; }
bool EndsWith(const Text* text, char c)
{
    return Length(text->body) >= 1 && text->body.begin[Length(text->body) - 1] == c;
}

// The game's 0x0090A915 (cdecl: a string, the number out): the number at
// its front (0x0090FE93).
void ParseNumber(const Text* text, uint32_t* out)
{
    *out = GameFn<uint32_t(__cdecl*)(const char*)>(addr::SC4_TextToUint32)(text->body.begin);
}

// The game's 0x0090A9EE (cdecl: a string, two dwords out): a length, the
// kind (+0: 1 for percent, a string ending in `%`, else 0) and the number
// (+4), the percent's from a copy without its `%`.
void ParseLength(const Text* text, uint32_t* out)
{
    using ToNumberFn = uint32_t(__cdecl*)(const char*);
    out[0] = 0;
    out[1] = 0;
    if (GameFn<uint8_t(__cdecl*)(const Text*, char)>(addr::HTMLText_EndsWith)(text, '%')) {
        out[0] = 1;
        Text number;
        CallThis<Text*>(addr::SC4String_FromBytes, &number, text->body.begin, Length(text->body) - 1);
        out[1] = GameFn<ToNumberFn>(addr::SC4_TextToUint32)(number.body.begin);
        if (number.body.begin)
            Free(number.body.begin);
    } else {
        out[1] = GameFn<ToNumberFn>(addr::SC4_TextToUint32)(text->body.begin);
    }
}

// ====================================================================================
// Pictures, children and WIDTH

// The body's picture set (0x009081BB, slot 0 of its image item at +0x28,
// ret 4: the picture): kept (+0x3C, the item's +0x14) and AddRef'd (slot
// 1); nothing to lay out again. The picture it had is not released.
uint8_t SetBodyPicture(char* item, char* picture)
{
    At<char*>(item, 0x14) = picture;
    if (picture)
        Virtual<uint32_t>(picture, 0x4);
    return 0;
}

// IMG's picture set (0x009082FB, slot 0 of its image item at +0x20, ret 4):
// the picture kept (+0x24, the item's +4) and AddRef'd; WIDTH and HEIGHT
// (+0x2C, +0x30) when none was given taken from it (slots 9 and 10), each
// asked of the picture as kept. 1 when either changed. The picture it had
// is not released.
uint32_t SetImagePicture(char* item, char* picture)
{
    const uint32_t height = At<uint32_t>(item, 0x10);
    const uint32_t width = At<uint32_t>(item, 0xC);
    At<char*>(item, 0x4) = picture;
    if (picture) {
        Virtual<uint32_t>(picture, 0x4);
        if (At<uint32_t>(item, 0xC) == 0)
            At<uint32_t>(item, 0xC) = Virtual<uint32_t>(At<char*>(item, 0x4), 0x24);
        if (At<uint32_t>(item, 0x10) == 0)
            At<uint32_t>(item, 0x10) = Virtual<uint32_t>(At<char*>(item, 0x4), 0x28);
    }
    return width != At<uint32_t>(item, 0xC) || height != At<uint32_t>(item, 0x10) ? 1 : 0;
}

// The containers' AddChild (0x0090888C, slot 1, ret 4: the child): the
// child pushed onto the children (+0x1C) by the game's push_back
// (0x008FF785), whose EAX it returns.
uint32_t AddChild(char* self, char* child)
{
    return CallThis<uint32_t>(addr::SC4PtrVector_PushBack8FF785, self + 0x1C, &child);
}

// TABLE's WIDTH (0x009083C5, ret 4: the attribute): a length (0x0090A9EE)
// into +0x2C.
void SetTableWidth(char* self, const Text* text)
{
    GameFn<void(__cdecl*)(const Text*, uint32_t*)>(addr::HTMLText_ParseLength)(
        text, reinterpret_cast<uint32_t*>(self + 0x2C));
}

// ====================================================================================
// The detours

#define OPENSC4_UNPAREN(...) __VA_ARGS__

// A thiscall function of the game: the detour is a fastcall one (EDX not
// read) that hands its arguments on.
#define OPENSC4_THIS_DETOUR(Ret, Name, Address, Ours, Params, Args)                                            \
    using Name##Fn = Ret(__thiscall*)(char* self OPENSC4_UNPAREN Params);                                    \
    Name##Fn g_##Name = nullptr;                                                                               \
    Ret __fastcall D##Name(char* self, void* OPENSC4_UNPAREN Params)                                         \
    {                                                                                                          \
        if (g_useGame)                                                                                         \
            return g_##Name(self OPENSC4_UNPAREN Args);                                                        \
        OPENSC4_FIRST(#Name " " #Address);                                                                     \
        return Ours(self OPENSC4_UNPAREN Args);                                                                \
    }

// A cdecl function of the game.
#define OPENSC4_CDECL_DETOUR(Ret, Name, Address, Ours, Params, Args)                                           \
    using Name##Fn = Ret(__cdecl*) Params;                                                                     \
    Name##Fn g_##Name = nullptr;                                                                               \
    Ret __cdecl D##Name Params                                                                                 \
    {                                                                                                          \
        if (g_useGame)                                                                                         \
            return g_##Name Args;                                                                              \
        OPENSC4_FIRST(#Name " " #Address);                                                                     \
        return Ours Args;                                                                                      \
    }

// Constructors.
OPENSC4_THIS_DETOUR(char*, ConstructNode, 0x0090847A, ConstructNode, (, uint32_t kind), (, kind))
OPENSC4_THIS_DETOUR(char*, ConstructImage, 0x0090850E, ConstructImage, (), ())
OPENSC4_THIS_DETOUR(char*, ConstructHeadLink, 0x0090856D, ConstructHeadLink, (), ())
OPENSC4_THIS_DETOUR(char*, ConstructMeta, 0x009085A7, ConstructMeta, (), ())
OPENSC4_THIS_DETOUR(char*, ConstructList, 0x00908704, ConstructList, (, uint32_t kind, uint32_t depth),
                    (, kind, depth))
OPENSC4_THIS_DETOUR(char*, ConstructTable, 0x00908723, ConstructTable, (), ())
OPENSC4_THIS_DETOUR(char*, ConstructCell, 0x00908781, ConstructCell, (), ())
OPENSC4_THIS_DETOUR(char*, ConstructLink, 0x009087A7, ConstructLink, (), ())
OPENSC4_THIS_DETOUR(char*, ConstructBody, 0x00908811, ConstructBody,
                    (, uint32_t link, uint32_t visited, uint32_t active), (, link, visited, active))

// The game's IMG, LINK and META constructors keep `this` in EDX across the
// base's (0x0090847A): ours of it keeps ECX and EDX.
__declspec(naked) void ConstructNodeThunk()
{
    __asm push ecx
    __asm push edx
    __asm push dword ptr [esp + 0xC]
    __asm call DConstructNode
    __asm pop edx
    __asm pop ecx
    __asm ret 4
}

// A text run.
OPENSC4_THIS_DETOUR(char*, ConstructText, 0x00908CA6, ConstructText,
                    (, char* doc, const Text* text, char* record, uint32_t number), (, doc, text, record, number))
OPENSC4_CDECL_DETOUR(void, CollapseText, 0x009089B0, CollapseText, (Text * text, uint32_t pre),
                     (text, (pre & 0xFF) != 0))
OPENSC4_THIS_DETOUR(char*, FindFont, 0x00908B04, FindFont, (, char* doc, char* record), (, doc, record))

// Destructors.
OPENSC4_THIS_DETOUR(void, DestroyContainer, 0x009086B7, DestroyContainer, (), ())
OPENSC4_THIS_DETOUR(void, DestroyImage, 0x00908289, DestroyImage, (), ())
OPENSC4_THIS_DETOUR(void, DestroyHeadLink, 0x0090834F, DestroyHeadLink, (), ())
OPENSC4_THIS_DETOUR(void, DestroyMeta, 0x0090838A, DestroyMeta, (), ())
OPENSC4_THIS_DETOUR(void, DestroyLink, 0x009087D6, DestroyLink, (), ())
OPENSC4_THIS_DETOUR(void, DestroyBody, 0x00908856, DestroyBody, (), ())
OPENSC4_THIS_DETOUR(void, DestroyTable, 0x009088B8, DestroyTable, (), ())
OPENSC4_THIS_DETOUR(void, DestroyText, 0x0090891E, DestroyText, (), ())

#define OPENSC4_DELETING(Name, Address, Destructor)                                                            \
    char* Name(char* self, uint32_t flags) { return DeleteNode(self, flags, Destructor); }                     \
    OPENSC4_THIS_DETOUR(char*, Name##Slot, Address, Name, (, uint32_t flags), (, flags))

OPENSC4_DELETING(DeleteContainer, 0x00908902, addr::HTMLContainerNode_Destruct)
OPENSC4_DELETING(DeleteImage, 0x00908551, addr::HTMLImageNode_Destruct)
OPENSC4_DELETING(DeleteHeadLink, 0x0090858B, addr::HTMLHeadLinkNode_Destruct)
OPENSC4_DELETING(DeleteMeta, 0x009085C4, addr::HTMLMetaNode_Destruct)
OPENSC4_DELETING(DeleteLink, 0x0090895C, addr::HTMLLinkNode_Destruct)
OPENSC4_DELETING(DeleteBody, 0x00908978, addr::HTMLBodyNode_Destruct)
OPENSC4_DELETING(DeleteTable, 0x0090889C, addr::HTMLTableNode_Destruct)
OPENSC4_DELETING(DeleteText, 0x00908994, addr::HTMLTextNode_Destruct)

// Colours.
OPENSC4_CDECL_DETOUR(void, ParseColour, 0x0090AA54, ParseColour, (const Text* text, uint32_t* colour),
                     (text, colour))
OPENSC4_THIS_DETOUR(void, SetLink, 0x009081D2, SetLink, (, const Text* text), (, text))
OPENSC4_THIS_DETOUR(void, SetActiveLink, 0x0090820F, SetActiveLink, (, const Text* text), (, text))
OPENSC4_THIS_DETOUR(void, SetVisitedLink, 0x0090824C, SetVisitedLink, (, const Text* text), (, text))

void* g_HexDigit = nullptr;

uint32_t __cdecl DHexDigit(uint32_t c)
{
    if (g_useGame)
        return g_callWithAl(c, reinterpret_cast<std::uintptr_t>(g_HexDigit));
    OPENSC4_FIRST("HexDigit 0x0090A801");
    return IsHexDigit(static_cast<char>(c)) ? 1 : 0;
}

// The game's colour parser hands the character over in AL and keeps the
// string in ECX across the call: ours keeps ECX and EDX.
__declspec(naked) void HexDigitThunk()
{
    __asm push ecx
    __asm push edx
    __asm movzx eax, al
    __asm push eax
    __asm call DHexDigit
    __asm add esp, 4
    __asm pop edx
    __asm pop ecx
    __asm ret
}

// Characters.
OPENSC4_CDECL_DETOUR(uint32_t, PreviousChar, 0x0090A8F5, PreviousChar, (const Text* text, uint32_t position),
                     (text, position))
OPENSC4_CDECL_DETOUR(uint32_t, NextChar, 0x0090A947, NextChar, (const Text* text, uint32_t position),
                     (text, position))
OPENSC4_CDECL_DETOUR(uint8_t, IsSpace, 0x0090A820, IsSpace, (const char* text, int32_t left), (text, left))
OPENSC4_CDECL_DETOUR(uint8_t, IsSpaceAt, 0x0090A929, IsSpaceAt, (const Text* text, uint32_t position),
                     (text, position))
OPENSC4_CDECL_DETOUR(uint8_t, HasPrefix, 0x0090A96C, HasPrefix,
                     (const Text* text, uint32_t count, const char* prefix), (text, count, prefix))
OPENSC4_CDECL_DETOUR(uint32_t, StartsWith, 0x0090A9AA, StartsWith, (const Text* text, char c), (text, c))
OPENSC4_CDECL_DETOUR(uint32_t, EndsWith, 0x0090A9CB, EndsWith, (const Text* text, char c), (text, c))
OPENSC4_CDECL_DETOUR(void, ParseNumber, 0x0090A915, ParseNumber, (const Text* text, uint32_t* out), (text, out))
OPENSC4_CDECL_DETOUR(void, ParseLength, 0x0090A9EE, ParseLength, (const Text* text, uint32_t* out), (text, out))

// Pictures, children and WIDTH.
OPENSC4_THIS_DETOUR(uint8_t, SetBodyPicture, 0x009081BB, SetBodyPicture, (, char* picture), (, picture))
OPENSC4_THIS_DETOUR(uint32_t, SetImagePicture, 0x009082FB, SetImagePicture, (, char* picture), (, picture))
OPENSC4_THIS_DETOUR(uint32_t, AddChild, 0x0090888C, AddChild, (, char* child), (, child))
OPENSC4_THIS_DETOUR(void, SetTableWidth, 0x009083C5, SetTableWidth, (, const Text* text), (, text))

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
    log::Write("html nodes: %s: %zu functions ours", name, N);
    return true;
}

#define OPENSC4_D(Name, Address) {Address, reinterpret_cast<void*>(&D##Name), Slot(&g_##Name)}

bool InstallConstructors(registry::Mode)
{
    const Detour detours[] = {
        {addr::HTMLNode_Construct, reinterpret_cast<void*>(&ConstructNodeThunk), Slot(&g_ConstructNode)},
        OPENSC4_D(ConstructImage, addr::HTMLImageNode_Construct),
        OPENSC4_D(ConstructHeadLink, addr::HTMLHeadLinkNode_Construct),
        OPENSC4_D(ConstructMeta, addr::HTMLMetaNode_Construct),
        OPENSC4_D(ConstructList, addr::HTMLListNode_Construct),
        OPENSC4_D(ConstructTable, addr::HTMLTableNode_Construct),
        OPENSC4_D(ConstructCell, addr::HTMLCellNode_Construct),
        OPENSC4_D(ConstructLink, addr::HTMLLinkNode_Construct),
        OPENSC4_D(ConstructBody, addr::HTMLBodyNode_Construct),
    };
    return InstallAll("constructors", detours);
}

bool InstallText(registry::Mode)
{
    const Detour detours[] = {
        OPENSC4_D(ConstructText, addr::HTMLTextNode_Construct),
        OPENSC4_D(CollapseText, addr::HTMLTextNode_CollapseText),
        OPENSC4_D(FindFont, addr::HTMLTextNode_FindFont),
    };
    return InstallAll("text constructor", detours);
}

bool InstallDestructors(registry::Mode)
{
    const Detour detours[] = {
        OPENSC4_D(DestroyContainer, addr::HTMLContainerNode_Destruct),
        OPENSC4_D(DestroyImage, addr::HTMLImageNode_Destruct),
        OPENSC4_D(DestroyHeadLink, addr::HTMLHeadLinkNode_Destruct),
        OPENSC4_D(DestroyMeta, addr::HTMLMetaNode_Destruct),
        OPENSC4_D(DestroyLink, addr::HTMLLinkNode_Destruct),
        OPENSC4_D(DestroyBody, addr::HTMLBodyNode_Destruct),
        OPENSC4_D(DestroyTable, addr::HTMLTableNode_Destruct),
        OPENSC4_D(DestroyText, addr::HTMLTextNode_Destruct),
        OPENSC4_D(DeleteContainerSlot, addr::HTMLContainerNode_Delete),
        OPENSC4_D(DeleteImageSlot, addr::HTMLImageNode_Delete),
        OPENSC4_D(DeleteHeadLinkSlot, addr::HTMLHeadLinkNode_Delete),
        OPENSC4_D(DeleteMetaSlot, addr::HTMLMetaNode_Delete),
        OPENSC4_D(DeleteLinkSlot, addr::HTMLLinkNode_Delete),
        OPENSC4_D(DeleteBodySlot, addr::HTMLBodyNode_Delete),
        OPENSC4_D(DeleteTableSlot, addr::HTMLTableNode_Delete),
        OPENSC4_D(DeleteTextSlot, addr::HTMLTextNode_Delete),
    };
    return InstallAll("destructors", detours);
}

bool InstallColours(registry::Mode)
{
    const Detour detours[] = {
        OPENSC4_D(ParseColour, addr::HTMLColor_Parse),
        {addr::HTMLColor_IsHexDigit, reinterpret_cast<void*>(&HexDigitThunk), &g_HexDigit},
        OPENSC4_D(SetLink, addr::HTMLBodyNode_SetLink),
        OPENSC4_D(SetActiveLink, addr::HTMLBodyNode_SetActiveLink),
        OPENSC4_D(SetVisitedLink, addr::HTMLBodyNode_SetVisitedLink),
    };
    return InstallAll("colours", detours);
}

bool InstallChars(registry::Mode)
{
    const Detour detours[] = {
        OPENSC4_D(PreviousChar, addr::HTMLText_PreviousChar),
        OPENSC4_D(NextChar, addr::HTMLText_NextChar),
        OPENSC4_D(IsSpace, addr::HTMLText_IsSpace),
        OPENSC4_D(IsSpaceAt, addr::HTMLText_IsSpaceAt),
        OPENSC4_D(HasPrefix, addr::HTMLText_HasPrefix),
        OPENSC4_D(StartsWith, addr::HTMLText_StartsWith),
        OPENSC4_D(EndsWith, addr::HTMLText_EndsWith),
        OPENSC4_D(ParseNumber, addr::HTMLText_ParseNumber),
        OPENSC4_D(ParseLength, addr::HTMLText_ParseLength),
    };
    return InstallAll("characters", detours);
}

bool InstallPictures(registry::Mode)
{
    const Detour detours[] = {
        OPENSC4_D(SetBodyPicture, addr::HTMLBodyNode_SetPicture),
        OPENSC4_D(SetImagePicture, addr::HTMLImageNode_SetPicture),
    };
    return InstallAll("pictures", detours);
}

bool InstallChildren(registry::Mode)
{
    const Detour detours[] = {
        OPENSC4_D(AddChild, addr::HTMLContainerNode_AddChild),
        OPENSC4_D(SetTableWidth, addr::HTMLTableNode_SetWidth),
    };
    return InstallAll("children and WIDTH", detours);
}

const bool g_registered[] = {
    registry::Register({"ui.html.nodes.ctors", "ui/html", registry::Kind::Detour, registry::Phase::Attach,
                        addr::HTMLNode_Construct,
                        "Our HTML nodes' constructors: the base node, IMG, LINK, META, lists, TABLE, TD, A and the "
                        "body (9 detours; docs/html.md)",
                        false, InstallConstructors}),
    registry::Register({"ui.html.nodes.ctors.text", "ui/html", registry::Kind::Detour, registry::Phase::Attach,
                        addr::HTMLTextNode_Construct,
                        "Our HTML text runs' constructor: entities, spaces and the font (3 detours; docs/html.md)",
                        false, InstallText}),
    registry::Register({"ui.html.nodes.dtors", "ui/html", registry::Kind::Detour, registry::Phase::Attach,
                        addr::HTMLContainerNode_Destruct,
                        "Our HTML nodes' destructors and deleting destructors (16 detours; docs/html.md)", false,
                        InstallDestructors}),
    registry::Register({"ui.html.colors", "ui/html", registry::Kind::Detour, registry::Phase::Attach,
                        addr::HTMLColor_Parse,
                        "Our HTML colours: the 16 names and #rrggbb, the body's link colours (5 detours; "
                        "docs/html.md)",
                        false, InstallColours}),
    registry::Register({"ui.html.chars", "ui/html", registry::Kind::Detour, registry::Phase::Attach,
                        addr::HTMLText_NextChar,
                        "Our HTML character helpers: the next and previous character, spaces, prefixes, numbers "
                        "and lengths (9 detours; docs/html.md)",
                        false, InstallChars}),
    registry::Register({"ui.html.nodes.pictures", "ui/html", registry::Kind::Detour, registry::Phase::Attach,
                        addr::HTMLImageNode_SetPicture,
                        "Our HTML pictures set: the body's background and IMG's, with IMG's size (2 detours; "
                        "docs/html.md)",
                        false, InstallPictures}),
    registry::Register({"ui.html.nodes.children", "ui/html", registry::Kind::Detour, registry::Phase::Attach,
                        addr::HTMLContainerNode_AddChild,
                        "Our HTML containers' AddChild and TABLE's WIDTH (2 detours; docs/html.md)", false,
                        InstallChildren}),
};

} // namespace
} // namespace opensc4::html_nodes
