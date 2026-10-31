// Our part of the HTML viewer's document code (docs/html.md, piece h2, the
// first half: "Ours: the document's nodes, handlers and tokenizer
// helpers"): the node constructors and destructors outside h5, the small
// tag handlers, the attribute setters they and the parser's handlers call,
// and the tokenizer's helpers, written by hand from the disassembly. Each
// function is a detour on the game's, in groups, off by default:
//
//   ui.html.document.ctors       BR's, HR's and INPUT's constructors, the
//                                containers' base and the 17 classes built
//                                on it (AREA to TH)
//   ui.html.document.dtors       the leaves' deleting destructor (BR, HR,
//                                INPUT) and TR's destructor and deleting
//                                destructor
//   ui.html.document.attributes  BGCOLOR of TABLE, TR and TD, the two ALIGN
//                                parsers, A's HREF, address and NAME, TD's
//                                NOWRAP, the string setters of IMG's ALT
//                                and META, the attribute check
//   ui.html.document.handlers    BR, INPUT, AREA, BASE, HR, IMG, LINK, META
//   ui.html.document.tokenizer   the tag's name split, trimming, spaces
//                                skipped, the string hash maps' hash code,
//                                bucket, find, erase and clear, GetAttribute
//
// The game's strings and containers are STLport's, through the game's own
// helpers (the string constructors, Substring, the bodies' erase, find,
// assign and resize, MakeUpper, Equals; the hash map's iterator and the
// string pair's destructor; operator new and delete, the allocator's Free).
// Ours call them with the same arguments in the same order, so the heap sees
// the same allocations and frees. Calls between these functions go through
// the game's addresses, so any mix of groups works.
//
// html_parser::g_useGame (set by the HTML tests) sends every detour to the
// game's function, so one run can do the same work through the game's code
// and through ours and compare.
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

namespace opensc4::html_document {
namespace {

using html_parser::g_useGame;

void FirstCall(const char* name, bool& logged)
{
    if (logged)
        return;
    logged = true;
    log::Write("html document: first %s", name);
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

template <typename R, typename... A>
R CallPlain(std::uintptr_t fn, A... a)
{
    return reinterpret_cast<R(__cdecl*)(A...)>(fn)(a...);
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
Text* Substring(Text* text, const Text* source, uint32_t position, uint32_t count)
{
    return CallThis<Text*>(addr::SC4String_Substring, text, source, position, count);
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
void MakeUpper(Text* text) { CallThis<Text*>(addr::cRZBaseString_MakeUpper, text); }
void Free(void* p) { CallPlain<void>(addr::SC4_Free, p); }
char* NewObject(std::size_t size) { return CallPlain<char*>(addr::SC4_operator_new, size); }
uint32_t TextToNumber(const char* text) { return CallPlain<uint32_t>(addr::SC4_TextToUint32, text); }

// The game's string compare, given the body (null for no string).
bool Equals(Text* text, const char* with)
{
    Body* const body = text ? &text->body : nullptr;
    return CallPlain<uint8_t>(addr::SC4String_Equals, body, with) != 0;
}

// A string of the game's own going out of scope: the vtable put back, its
// characters freed (the game's inlined destructor).
void Release(Text* text)
{
    text->vtable = static_cast<uint32_t>(addr::SC4String_Vtable);
    if (text->body.begin)
        Free(text->body.begin);
}

// A new string of the game's (0x14 bytes from operator new), the whole of
// `source`; null when the allocation failed.
Text* NewCopy(const Text* source)
{
    char* const memory = NewObject(0x14);
    return memory ? Substring(reinterpret_cast<Text*>(memory), source, 0, kAll) : nullptr;
}

// The character helpers, through the game's addresses (ours or the game's).
uint32_t CallNextChar(const Text* text, uint32_t position)
{
    return CallPlain<uint32_t>(addr::HTMLText_NextChar, text, position);
}
bool CallIsSpaceAt(const Text* text, uint32_t position)
{
    return CallPlain<uint8_t>(addr::HTMLText_IsSpaceAt, text, position) != 0;
}

// ====================================================================================
// The constructors

constexpr uint32_t kLeafVtable = 0x00ACD504;

void CallConstructNode(char* self, uint32_t kind) { CallThis<char*>(addr::HTMLNode_Construct, self, kind); }
void CallConstructContainer(char* self, uint32_t kind)
{
    CallThis<char*>(addr::HTMLContainerNode_Construct, self, kind);
}

// BR (0x008FE5F9, kind 0x16) and INPUT (0x008FE63A, kind 0x1D): the base
// node and a vtable. 0x1C bytes.
char* ConstructBreak(char* self)
{
    CallConstructNode(self, 0x16);
    At<uint32_t>(self, 0x0) = 0x00ACD530;
    return self;
}

char* ConstructInput(char* self)
{
    CallConstructNode(self, 0x1D);
    At<uint32_t>(self, 0x0) = 0x00ACD588;
    return self;
}

// HR (0x008FE60D, kind 5, 0x2C bytes): ALIGN (+0x1C) 3, SIZE (+0x20) 1,
// WIDTH (+0x24) 100 and its percent flag (+0x28) set.
char* ConstructRule(char* self)
{
    CallConstructNode(self, 5);
    At<uint32_t>(self, 0x0) = 0x00ACD55C;
    At<uint32_t>(self, 0x1C) = 3;
    At<uint32_t>(self, 0x20) = 1;
    At<uint32_t>(self, 0x24) = 100;
    At<uint8_t>(self, 0x28) = 1;
    return self;
}

// The containers' base (0x008FECBD, ret 4: the kind): the base node, vtable
// 0x00ACD5E8, the children's vector (+0x1C) empty.
char* ConstructContainer(char* self, uint32_t kind)
{
    CallConstructNode(self, kind);
    At<uint32_t>(self, 0x0) = 0x00ACD5E8;
    At<uint32_t>(self, 0x1C) = 0;
    At<uint32_t>(self, 0x20) = 0;
    At<uint32_t>(self, 0x24) = 0;
    return self;
}

// The containers with nothing of their own but a kind and a vtable.
template <uint32_t Kind, uint32_t Vtable>
char* ConstructPlain(char* self)
{
    CallConstructContainer(self, Kind);
    At<uint32_t>(self, 0x0) = Vtable;
    return self;
}

// AREA (0x008FECE0, kind 0x18) and MAP (0x008FEDAC, kind 0x17): +0x28
// (AREA's SHAPE, MAP's NAME) none. 0x2C bytes.
template <uint32_t Kind, uint32_t Vtable>
char* ConstructWithField(char* self)
{
    CallConstructContainer(self, Kind);
    At<uint32_t>(self, 0x28) = 0;
    At<uint32_t>(self, 0x0) = Vtable;
    return self;
}

// TR (0x008FEE14, kind 0xF, 0x3C bytes): its cells' vector (+0x28) empty,
// BGCOLOR's flag (+0x38) none.
char* ConstructRow(char* self)
{
    CallConstructContainer(self, 0xF);
    At<uint32_t>(self, 0x0) = 0x00ACD8A8;
    At<uint32_t>(self, 0x28) = 0;
    At<uint32_t>(self, 0x2C) = 0;
    At<uint32_t>(self, 0x30) = 0;
    At<uint8_t>(self, 0x38) = 0;
    return self;
}

// ====================================================================================
// The destructors

// The leaves' deleting destructor (0x008FE861, slot 0 of BR, HR and INPUT
// and of the leaves' base): the base's vtable put back, the node deleted
// with flag 1.
char* DeleteLeaf(char* self, uint32_t flags)
{
    At<uint32_t>(self, 0x0) = kLeafVtable;
    if (flags & 1)
        CallPlain<void>(addr::SC4_operator_delete, self);
    return self;
}

// TR's destructor (0x008FF16E): its cells' storage freed, then the
// containers' destructor; no vtable of its own is written.
void DestroyRow(char* self)
{
    if (At<void*>(self, 0x28))
        Free(At<void*>(self, 0x28));
    CallThis<void>(addr::HTMLContainerNode_Destruct, self);
}

// TR's deleting destructor (0x008FF152, slot 0).
char* DeleteRow(char* self, uint32_t flags)
{
    CallThis<void>(addr::HTMLRowNode_Destruct, self);
    if (flags & 1)
        CallPlain<void>(addr::SC4_operator_delete, self);
    return self;
}

// ====================================================================================
// The attribute setters

// BGCOLOR of TABLE (0x008FE64E: +0x44, its flag +0x48) and of TR and TD
// (0x008FE68F: +0x34, +0x38): the colour parsed into 0xFF000000, then made
// by the framework's colour service (slot 34, +0x88) from its red, green
// and blue. The game pushes the three as the dwords at the colour's bytes
// 2, 1 and 0 of a frame whose next dword holds ECX (the node), so red's
// upper bytes are the alpha and the node's address, green's red, the alpha
// and the address; the service reads bytes.
void SetBackground(char* self, const Text* text, std::size_t field)
{
    uint32_t colour = 0xFF000000u;
    CallPlain<void>(addr::HTMLColor_Parse, text, &colour);
    char* const colours = CallPlain<char*>(addr::SC4_Singleton913C1A);
    const uint32_t self32 = static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(self));
    const uint32_t red = (colour >> 16) | (self32 << 16);
    const uint32_t green = (colour >> 8) | (self32 << 24);
    At<uint32_t>(self, field) = Virtual<uint32_t>(colours, 0x88, red, green, colour);
    At<uint8_t>(self, field + 4) = 1;
}

void SetTableBackground(char* self, const Text* text) { SetBackground(self, text, 0x44); }
void SetNodeBackground(char* self, const Text* text) { SetBackground(self, text, 0x34); }

// The cells' ALIGN (0x008FE8D6, cdecl, the string made upper case first):
// LEFT 0, RIGHT 2, CENTER 1, anything else 0.
int32_t ParseAlign(Text* text)
{
    MakeUpper(text);
    if (Equals(text, "LEFT"))
        return 0;
    if (Equals(text, "RIGHT"))
        return 2;
    return Equals(text, "CENTER") ? 1 : 0;
}

// HR's and IMG's ALIGN (0x008FE92B): LEFT 0, RIGHT 1, TOP 2, BOTTOM 4,
// MIDDLE 3, anything else 4.
int32_t ParseImageAlign(Text* text)
{
    MakeUpper(text);
    if (Equals(text, "LEFT"))
        return 0;
    if (Equals(text, "RIGHT"))
        return 1;
    if (Equals(text, "TOP"))
        return 2;
    if (Equals(text, "BOTTOM"))
        return 4;
    return Equals(text, "MIDDLE") ? 3 : 4;
}

// A's HREF, the address resolved from it and NAME (0x008FFE8F, 0x008FFEB8,
// 0x008FFEE1): a new string of the whole text at +0x28, +0x2C, +0x30; the
// one there before is not freed.
void SetHref(char* self, const Text* text) { At<Text*>(self, 0x28) = NewCopy(text); }
void SetAddress(char* self, const Text* text) { At<Text*>(self, 0x2C) = NewCopy(text); }
void SetName(char* self, const Text* text) { At<Text*>(self, 0x30) = NewCopy(text); }

// TD's NOWRAP (0x008FFF0A): the flag (+0x39) set; a value is copied and made
// upper case, and the copy dropped.
void SetNoWrap(char* self, const Text* text)
{
    At<uint8_t>(self, 0x39) = 1;
    if (Length(text->body) == 0)
        return;
    Text copy;
    Substring(&copy, text, 0, kAll);
    MakeUpper(&copy);
    Release(&copy);
}

// A new empty string at `field` given the text (0x00900519: +0x28, MAP's
// NAME and IMG's ALT; 0x0090053E, 0x00900563, 0x00900588: META's NAME,
// CONTENT and HTTP-EQUIV at +0x1C, +0x20, +0x24). The game assigns to what
// operator new gave, null or not.
void SetString(char* self, const Text* text, std::size_t field)
{
    char* const memory = NewObject(0x14);
    Text* string = nullptr;
    if (memory) {
        MakeEmpty(reinterpret_cast<Text*>(memory));
        string = reinterpret_cast<Text*>(memory);
    }
    At<Text*>(self, field) = string;
    AssignText(string, text);
}

void SetString28(char* self, const Text* text) { SetString(self, text, 0x28); }
void SetMetaName(char* self, const Text* text) { SetString(self, text, 0x1C); }
void SetMetaContent(char* self, const Text* text) { SetString(self, text, 0x20); }
void SetMetaHttpEquiv(char* self, const Text* text) { SetString(self, text, 0x24); }

// The hash map's iterator: a node and the map.
struct Iterator {
    char* node;
    char* map;
};

constexpr std::size_t kAttributes = 0x114;
constexpr std::size_t kAttributeCount = 0x124;

// After a tag's attributes are read (0x008FF7FD, ret 8: the handler's name
// and the tag's, neither read): the attribute map walked from its first
// node to its end, doing nothing on the way.
void CheckAttributes(char* doc, const char* /*handler*/, const char* /*tag*/)
{
    if (At<uint32_t>(doc, kAttributeCount) == 0)
        return;
    Iterator it{};
    CallThis<Iterator*>(addr::cGZDBSegmentPackedFile_KeyTableBegin, doc + kAttributes, &it);
    while (it.node)
        CallThis<Iterator*>(addr::HTMLAttributes_Next, &it);
}

// ====================================================================================
// The handlers

void AddChild(char* parent, char* child) { Virtual<void*>(parent, 0x4, child); }

bool Attribute(char* doc, const char* name, Text* value)
{
    return CallThis<uint8_t>(addr::HTMLDocument_GetAttribute, doc, name, value, uint32_t{1}) != 0;
}

void Check(char* doc, const char* handler, const char* tag)
{
    CallThis<void>(addr::HTMLDocument_CheckAttributes, doc, handler, tag);
}

char* NewNode(std::size_t size, std::uintptr_t construct)
{
    char* const memory = NewObject(size);
    return memory ? CallThis<char*>(construct, memory) : nullptr;
}

constexpr std::size_t kBase = 0xB8;      // the base address (a string of its own)
constexpr std::size_t kHeadLinks = 0xBC; // LINK's nodes
constexpr std::size_t kMetas = 0xAC;     // META's nodes
constexpr std::size_t kAlign = 0x158;    // the alignment the text takes

void PushPointer(char* vector, char** value) { CallThis<void>(addr::SC4PtrVector_PushBack8FF785, vector, value); }

// An address the page gives resolved against the document's base: the
// game's URL object (0xC0 bytes on the frame).
struct Url {
    alignas(4) char bytes[0xC0];
};

void ResolveUrl(Url* url, const Text* address, const Text* base)
{
    CallThis<void*>(addr::SC4Url_Construct, url);
    CallThis<void*>(addr::SC4Url_Resolve, url, address, base);
}

// BR (0x008FF725, ret 4: the parent): the node, an empty string made and
// dropped, the node to the parent.
bool ParseBreak(char* /*doc*/, char* parent)
{
    char* const node = NewNode(0x1C, addr::HTMLBreakNode_Construct);
    Text unused;
    MakeEmpty(&unused);
    AddChild(parent, node);
    Release(&unused);
    return true;
}

// INPUT (0x008FF82E): the node, the attributes checked, the node to the
// parent.
bool ParseInput(char* doc, char* parent)
{
    char* const node = NewNode(0x1C, addr::HTMLInputNode_Construct);
    Check(doc, "ParseINPUT", "INPUT");
    AddChild(parent, node);
    return true;
}

// AREA (0x0090083A): SHAPE (+0x28) upper case, DEFAULT 1, RECT 2, CIRCLE 3,
// POLY 4 (else as it was); HREF is not read.
bool ParseArea(char* doc, char* parent)
{
    char* const node = NewNode(0x2C, addr::HTMLAreaNode_Construct);
    Text value;
    MakeEmpty(&value);
    if (Attribute(doc, "SHAPE", &value)) {
        MakeUpper(&value);
        if (Equals(&value, "DEFAULT"))
            At<uint32_t>(node, 0x28) = 1;
        else if (Equals(&value, "RECT"))
            At<uint32_t>(node, 0x28) = 2;
        else if (Equals(&value, "CIRCLE"))
            At<uint32_t>(node, 0x28) = 3;
        else if (Equals(&value, "POLY"))
            At<uint32_t>(node, 0x28) = 4;
    }
    Check(doc, "ParseAREA", "AREA");
    AddChild(parent, node);
    Release(&value);
    return true;
}

// BASE (0x0090092D, no arguments): HREF becomes the document's base
// (0x00900302).
bool ParseBase(char* doc)
{
    Text value;
    MakeEmpty(&value);
    if (Attribute(doc, "HREF", &value))
        CallThis<void>(addr::HTMLDocument_SetBase, doc, static_cast<const Text*>(&value));
    Release(&value);
    return true;
}

// HR (0x0090097A): the node to the parent first, then ALIGN (+0x1C), SIZE
// (+0x20) and WIDTH (+0x24, a number; a `%` sets the percent flag, +0x28,
// which the constructor has set already, and ends the number).
bool ParseRule(char* doc, char* parent)
{
    char* const node = NewNode(0x2C, addr::HTMLRuleNode_Construct);
    AddChild(parent, node);
    Text value;
    MakeEmpty(&value);
    if (Attribute(doc, "ALIGN", &value))
        At<int32_t>(node, 0x1C) = CallPlain<int32_t>(addr::HTMLText_ParseImageAlign, &value);
    if (Attribute(doc, "SIZE", &value))
        At<uint32_t>(node, 0x20) = TextToNumber(value.body.begin);
    if (Attribute(doc, "WIDTH", &value)) {
        const uint32_t percent = FindChar(&value.body, '%', 0);
        if (percent < Length(value.body)) {
            At<uint8_t>(node, 0x28) = 1;
            Erase(&value.body, percent, kAll);
        }
        At<uint32_t>(node, 0x24) = TextToNumber(value.body.begin);
    }
    Release(&value);
    return true;
}

// IMG (0x00900A58): nothing without SRC (false). The node; SRC resolved
// against the base when there is one (else the address stays empty); ALT
// (+0x28), WIDTH (+0x2C), HEIGHT (+0x30), ALIGN (+0x34), the document's
// alignment (+0x38); the attributes checked, the node to the parent, and
// its image item (+0x20) handed to the document's slot 16 with the address.
bool ParseImage(char* doc, char* parent)
{
    Text value;
    Text source;
    Text address;
    MakeEmpty(&value);
    MakeEmpty(&source);
    MakeEmpty(&address);
    if (!Attribute(doc, "SRC", &source)) {
        Release(&address);
        Release(&source);
        Release(&value);
        return false;
    }
    char* const node = NewNode(0x3C, addr::HTMLImageNode_Construct);
    if (At<Text*>(doc, kBase)) {
        Url url;
        ResolveUrl(&url, &source, At<Text*>(doc, kBase));
        AssignText(&address, CallThis<const Text*>(addr::SC4Url_Text, &url));
        CallThis<void>(addr::SC4Url_Destroy, &url);
    }
    if (Attribute(doc, "ALT", &value))
        CallThis<void>(addr::HTMLMapNode_SetName, node, static_cast<const Text*>(&value));
    if (Attribute(doc, "WIDTH", &value))
        At<uint32_t>(node, 0x2C) = TextToNumber(value.body.begin);
    if (Attribute(doc, "HEIGHT", &value))
        At<uint32_t>(node, 0x30) = TextToNumber(value.body.begin);
    if (Attribute(doc, "ALIGN", &value))
        At<int32_t>(node, 0x34) = CallPlain<int32_t>(addr::HTMLText_ParseImageAlign, &value);
    At<uint32_t>(node, 0x38) = At<uint32_t>(doc, kAlign);
    Check(doc, "ParseIMG", "IMG");
    AddChild(parent, node);
    Virtual<void>(doc, 0x40, node + 0x20, &address);
    Release(&address);
    Release(&source);
    Release(&value);
    return true;
}

// LINK (0x00900C31, no arguments; true either way): nothing without HREF.
// The node; HREF (+0x24), resolved against the base when there is one
// (+0x28, left as the heap had it otherwise), the flag +0x20 set when there
// is no REV, TITLE (+0x1C); the node onto the document's list (+0xBC).
bool ParseHeadLink(char* doc)
{
    Text value;
    MakeEmpty(&value);
    if (Attribute(doc, "HREF", &value)) {
        char* node = NewNode(0x2C, addr::HTMLHeadLinkNode_Construct);
        At<Text*>(node, 0x24) = NewCopy(&value);
        if (At<Text*>(doc, kBase)) {
            Url url;
            ResolveUrl(&url, &value, At<Text*>(doc, kBase));
            char* const memory = NewObject(0x14);
            Text* resolved = nullptr;
            if (memory)
                resolved = Substring(reinterpret_cast<Text*>(memory), CallThis<const Text*>(addr::SC4Url_Text, &url),
                                     0, kAll);
            At<Text*>(node, 0x28) = resolved;
            CallThis<void>(addr::SC4Url_Destroy, &url);
        }
        At<uint8_t>(node, 0x20) = Attribute(doc, "REV", &value) ? 0 : 1;
        if (Attribute(doc, "TITLE", &value))
            At<Text*>(node, 0x1C) = NewCopy(&value);
        PushPointer(doc + kHeadLinks, &node);
    }
    Release(&value);
    return true;
}

// META (0x00900D78, no arguments): the node; NAME, CONTENT and HTTP-EQUIV
// through their setters; the attributes checked; with no CONTENT the node
// is deleted (false), else it goes onto the document's list (+0xAC).
bool ParseMeta(char* doc)
{
    char* node = NewNode(0x28, addr::HTMLMetaNode_Construct);
    Text value;
    MakeEmpty(&value);
    if (Attribute(doc, "NAME", &value))
        CallThis<void>(addr::HTMLMetaNode_SetName, node, static_cast<const Text*>(&value));
    if (Attribute(doc, "CONTENT", &value))
        CallThis<void>(addr::HTMLMetaNode_SetContent, node, static_cast<const Text*>(&value));
    if (Attribute(doc, "HTTP-EQUIV", &value))
        CallThis<void>(addr::HTMLMetaNode_SetHttpEquiv, node, static_cast<const Text*>(&value));
    Check(doc, "ParseMETA", "META");
    if (!At<Text*>(node, 0x20)) {
        Virtual<char*>(node, 0x0, uint32_t{1});
        Release(&value);
        return false;
    }
    PushPointer(doc + kMetas, &node);
    Release(&value);
    return true;
}

// ====================================================================================
// The tokenizer's helpers

// The string hash maps (the tag table, the attribute map): the hash and
// key-equality functors (+0, +1), the buckets' vector (+4 to +0xC), the
// count (+0x10). A node is the next node (+0), the key (+4, a string) and
// the value (+0x18).
std::uint32_t Buckets(char* map) { return static_cast<uint32_t>(At<char**>(map, 0x8) - At<char**>(map, 0x4)); }
char*& Bucket(char* map, uint32_t i) { return At<char**>(map, 0x4)[i]; }

// The hash code (0x008FE87E, ret 4, ECX the functor, not read): the sum of
// the key's characters, each sign-extended.
uint32_t Hash(char* /*self*/, const Text* key)
{
    uint32_t sum = 0;
    for (const char* at = key->body.begin; at < key->body.end; ++at)
        sum += static_cast<uint32_t>(static_cast<int32_t>(*at));
    return sum;
}

uint32_t CallHash(char* map, const Text* key) { return CallThis<uint32_t>(addr::HTMLStringHash_Hash, map, key); }

// The bucket of a key (0x008FEA32).
uint32_t BucketOf(char* map, const Text* key) { return CallHash(map, key) % Buckets(map); }

// The node of a key, or null (0x008FEAD9, the tag table's and the attribute
// map's find): the key compared by the equality functor (0x0063C020).
char* Find(char* map, const Text* key)
{
    const uint32_t i = CallHash(map, key) % Buckets(map);
    char* node = Bucket(map, i);
    while (node) {
        if (CallThis<uint32_t>(addr::SC4StringEqual_Call, map + 1, node + 4, key) & 0xFF)
            break;
        node = At<char*>(node, 0x0);
    }
    return node;
}

// A node deleted (0x008FFAA6, ret 4): its key and value destroyed, then
// freed (the destructor runs before the test for null, as the game's does).
void DeleteMapNode(char* /*map*/, char* node)
{
    CallThis<void>(addr::SC4StringPair_Destroy, node + 4);
    if (node)
        Free(node);
}

void CallDeleteMapNode(char* map, char* node) { CallThis<void>(addr::HTMLAttributes_DeleteNode, map, node); }

// The node an iterator points to unlinked from its bucket and deleted
// (0x008FFD78, ret 4); a node not in its bucket is left alone.
void EraseNode(char* map, const Iterator* it)
{
    char* const node = it->node;
    if (!node)
        return;
    const uint32_t i = CallThis<uint32_t>(addr::HTMLStringHash_Bucket, map, reinterpret_cast<const Text*>(node + 4));
    char*& slot = Bucket(map, i);
    char* const first = slot;
    if (first == node) {
        slot = At<char*>(first, 0x0);
        CallDeleteMapNode(map, first);
        --At<uint32_t>(map, 0x10);
        return;
    }
    char* previous = first;
    for (char* at = At<char*>(first, 0x0); at; previous = at, at = At<char*>(at, 0x0)) {
        if (at == node) {
            At<char*>(previous, 0x0) = At<char*>(at, 0x0);
            CallDeleteMapNode(map, at);
            --At<uint32_t>(map, 0x10);
            return;
        }
    }
}

// Erase at an iterator given by value (0x009001C9, ret 8).
void EraseAt(char* map, char* node, char* itsMap)
{
    const Iterator it{node, itsMap};
    CallThis<void>(addr::HTMLAttributes_EraseNode, map, &it);
}

// Every node deleted, the buckets kept and emptied, the count 0
// (0x008FFDC8).
void Clear(char* map)
{
    for (uint32_t i = 0; i < Buckets(map); ++i) {
        for (char* at = Bucket(map, i); at;) {
            char* const next = At<char*>(at, 0x0);
            CallDeleteMapNode(map, at);
            at = next;
        }
        Bucket(map, i) = nullptr;
    }
    At<uint32_t>(map, 0x10) = 0;
}

// The value of the current tag's attribute `name` (upper case) into
// `value` (0x0090072C, ret 0xC): true and, when asked, the attribute taken
// out of the map; false with `value` emptied when the tag has none.
bool GetAttribute(char* doc, const char* name, Text* value, uint8_t take)
{
    Text key;
    MakeFrom(&key, name);
    char* const map = doc + kAttributes;
    char* const node = CallThis<char*>(addr::HTMLTagTable_Find, map, static_cast<const Text*>(&key));
    if (key.body.begin)
        Free(key.body.begin);
    if (!node) {
        Erase(&value->body, 0, kAll);
        return false;
    }
    AssignText(value, reinterpret_cast<const Text*>(node + 0x18));
    if (take)
        CallThis<void>(addr::HTMLAttributes_Erase, map, node, map);
    return true;
}

// Trims the spaces off both ends of a string (0x008FEB74, cdecl): at the
// front by characters, at the end byte by byte.
void Trim(Text* text)
{
    Body* const body = &text->body;
    uint32_t n = Length(*body);
    if (n == 0)
        return;
    uint32_t i = 0;
    while (CallIsSpaceAt(text, i)) {
        i = CallNextChar(text, i);
        if (i >= n)
            break;
    }
    if (i > 0)
        Erase(body, 0, i);
    n = Length(*body);
    if (n == 0)
        return;
    for (uint32_t last = n - 1; CallIsSpaceAt(text, last);) {
        Erase(body, last, 1);
        --n;
        --last;
        if (n == 0)
            break;
    }
}

void CallTrim(Text* text) { CallPlain<void>(addr::HTMLText_Trim, text); }

// A tag's text split at its first space into the name (made upper case) and
// the rest (0x00900138, cdecl), both trimmed.
void SplitTagName(Text* tag, Text* rest)
{
    const uint32_t n = Length(tag->body);
    uint32_t i = 0;
    bool split = false;
    if (n != 0) {
        do {
            if (CallIsSpaceAt(tag, i)) {
                const uint32_t next = CallNextChar(tag, i);
                CallThis<Body*>(addr::SC4StringBody_AssignBody, &rest->body, static_cast<const Body*>(&tag->body),
                                next, n - next);
                CallThis<Body*>(addr::SC4String_ResizeBody, &tag->body, i, '\0');
                split = true;
                break;
            }
            i = CallNextChar(tag, i);
        } while (i < n);
    }
    if (!split)
        Erase(&rest->body, 0, kAll);
    CallTrim(tag);
    MakeUpper(tag);
    CallTrim(rest);
}

// The page's position (+0x194 in the page, +0x180) moved past spaces
// (0x008FE7B8).
void SkipSpaces(char* doc)
{
    const Text* const page = &At<Text>(doc, 0x180);
    uint32_t& position = At<uint32_t>(doc, 0x194);
    while (CallIsSpaceAt(page, position))
        position = CallNextChar(page, position);
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
OPENSC4_THIS_DETOUR(char*, ConstructBreak, 0x008FE5F9, ConstructBreak, (), ())
OPENSC4_THIS_DETOUR(char*, ConstructRule, 0x008FE60D, ConstructRule, (), ())
OPENSC4_THIS_DETOUR(char*, ConstructInput, 0x008FE63A, ConstructInput, (), ())
OPENSC4_THIS_DETOUR(char*, ConstructContainer, 0x008FECBD, ConstructContainer, (, uint32_t kind), (, kind))
OPENSC4_THIS_DETOUR(char*, ConstructArea, 0x008FECE0, (ConstructWithField<0x18, 0x00ACD614>), (), ())
OPENSC4_THIS_DETOUR(char*, ConstructBlink, 0x008FECF8, (ConstructPlain<0x22, 0x00ACD640>), (), ())
OPENSC4_THIS_DETOUR(char*, ConstructQuote, 0x008FED0C, (ConstructPlain<0x14, 0x00ACD66C>), (), ())
OPENSC4_THIS_DETOUR(char*, ConstructDD, 0x008FED20, (ConstructPlain<0xA, 0x00ACD698>), (), ())
OPENSC4_THIS_DETOUR(char*, ConstructAlign, 0x008FED34, (ConstructPlain<0x13, 0x00ACD6C4>), (), ())
OPENSC4_THIS_DETOUR(char*, ConstructDT, 0x008FED48, (ConstructPlain<0x9, 0x00ACD6F0>), (), ())
OPENSC4_THIS_DETOUR(char*, ConstructForm, 0x008FED5C, (ConstructPlain<0x1C, 0x00ACD71C>), (), ())
OPENSC4_THIS_DETOUR(char*, ConstructHeading, 0x008FED70, (ConstructPlain<0x2, 0x00ACD748>), (), ())
OPENSC4_THIS_DETOUR(char*, ConstructItem, 0x008FED84, (ConstructPlain<0xC, 0x00ACD774>), (), ())
OPENSC4_THIS_DETOUR(char*, ConstructItemBody, 0x008FED98, (ConstructPlain<0xC, 0x00ACD7A0>), (), ())
OPENSC4_THIS_DETOUR(char*, ConstructMap, 0x008FEDAC, (ConstructWithField<0x17, 0x00ACD7CC>), (), ())
OPENSC4_THIS_DETOUR(char*, ConstructOption, 0x008FEDC4, (ConstructPlain<0x1F, 0x00ACD7F8>), (), ())
OPENSC4_THIS_DETOUR(char*, ConstructParagraph, 0x008FEDD8, (ConstructPlain<0x15, 0x00ACD824>), (), ())
OPENSC4_THIS_DETOUR(char*, ConstructSelect, 0x008FEDEC, (ConstructPlain<0x1E, 0x00ACD850>), (), ())
OPENSC4_THIS_DETOUR(char*, ConstructSpan, 0x008FEE00, (ConstructPlain<0x23, 0x00ACD87C>), (), ())
OPENSC4_THIS_DETOUR(char*, ConstructRow, 0x008FEE14, ConstructRow, (), ())
OPENSC4_THIS_DETOUR(char*, ConstructHeaderCell, 0x008FEE36, (ConstructPlain<0x11, 0x00ACD8D4>), (), ())

// Destructors.
OPENSC4_THIS_DETOUR(char*, DeleteLeaf, 0x008FE861, DeleteLeaf, (, uint32_t flags), (, flags))
OPENSC4_THIS_DETOUR(void, DestroyRow, 0x008FF16E, DestroyRow, (), ())
OPENSC4_THIS_DETOUR(char*, DeleteRow, 0x008FF152, DeleteRow, (, uint32_t flags), (, flags))

// Attributes.
OPENSC4_THIS_DETOUR(void, SetTableBackground, 0x008FE64E, SetTableBackground, (, const Text* text), (, text))
OPENSC4_THIS_DETOUR(void, SetNodeBackground, 0x008FE68F, SetNodeBackground, (, const Text* text), (, text))
OPENSC4_CDECL_DETOUR(int32_t, ParseAlign, 0x008FE8D6, ParseAlign, (Text * text), (text))
OPENSC4_CDECL_DETOUR(int32_t, ParseImageAlign, 0x008FE92B, ParseImageAlign, (Text * text), (text))
OPENSC4_THIS_DETOUR(void, SetHref, 0x008FFE8F, SetHref, (, const Text* text), (, text))
OPENSC4_THIS_DETOUR(void, SetAddress, 0x008FFEB8, SetAddress, (, const Text* text), (, text))
OPENSC4_THIS_DETOUR(void, SetName, 0x008FFEE1, SetName, (, const Text* text), (, text))
OPENSC4_THIS_DETOUR(void, SetNoWrap, 0x008FFF0A, SetNoWrap, (, const Text* text), (, text))
OPENSC4_THIS_DETOUR(void, SetString28, 0x00900519, SetString28, (, const Text* text), (, text))
OPENSC4_THIS_DETOUR(void, SetMetaName, 0x0090053E, SetMetaName, (, const Text* text), (, text))
OPENSC4_THIS_DETOUR(void, SetMetaContent, 0x00900563, SetMetaContent, (, const Text* text), (, text))
OPENSC4_THIS_DETOUR(void, SetMetaHttpEquiv, 0x00900588, SetMetaHttpEquiv, (, const Text* text), (, text))
OPENSC4_THIS_DETOUR(void, CheckAttributes, 0x008FF7FD, CheckAttributes, (, const char* handler, const char* tag),
                    (, handler, tag))

// Handlers.
OPENSC4_THIS_DETOUR(bool, ParseBreak, 0x008FF725, ParseBreak, (, char* parent), (, parent))
OPENSC4_THIS_DETOUR(bool, ParseInput, 0x008FF82E, ParseInput, (, char* parent), (, parent))
OPENSC4_THIS_DETOUR(bool, ParseArea, 0x0090083A, ParseArea, (, char* parent), (, parent))
OPENSC4_THIS_DETOUR(bool, ParseBase, 0x0090092D, ParseBase, (), ())
OPENSC4_THIS_DETOUR(bool, ParseRule, 0x0090097A, ParseRule, (, char* parent), (, parent))
OPENSC4_THIS_DETOUR(bool, ParseImage, 0x00900A58, ParseImage, (, char* parent), (, parent))
OPENSC4_THIS_DETOUR(bool, ParseHeadLink, 0x00900C31, ParseHeadLink, (), ())
OPENSC4_THIS_DETOUR(bool, ParseMeta, 0x00900D78, ParseMeta, (), ())

// The tokenizer's helpers.
OPENSC4_THIS_DETOUR(uint32_t, Hash, 0x008FE87E, Hash, (, const Text* key), (, key))
OPENSC4_THIS_DETOUR(uint32_t, BucketOf, 0x008FEA32, BucketOf, (, const Text* key), (, key))
OPENSC4_THIS_DETOUR(char*, Find, 0x008FEAD9, Find, (, const Text* key), (, key))
OPENSC4_THIS_DETOUR(void, DeleteMapNode, 0x008FFAA6, DeleteMapNode, (, char* node), (, node))
OPENSC4_THIS_DETOUR(void, EraseNode, 0x008FFD78, EraseNode, (, const Iterator* it), (, it))
OPENSC4_THIS_DETOUR(void, EraseAt, 0x009001C9, EraseAt, (, char* node, char* itsMap), (, node, itsMap))
OPENSC4_THIS_DETOUR(void, Clear, 0x008FFDC8, Clear, (), ())
OPENSC4_THIS_DETOUR(bool, GetAttribute, 0x0090072C, GetAttribute, (, const char* name, Text* value, uint8_t take),
                    (, name, value, take))
OPENSC4_CDECL_DETOUR(void, Trim, 0x008FEB74, Trim, (Text * text), (text))
OPENSC4_CDECL_DETOUR(void, SplitTagName, 0x00900138, SplitTagName, (Text * tag, Text* rest), (tag, rest))
OPENSC4_THIS_DETOUR(void, SkipSpaces, 0x008FE7B8, SkipSpaces, (), ())

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
    log::Write("html document: %s: %zu functions ours", name, N);
    return true;
}

#define OPENSC4_D(Name, Address) {Address, reinterpret_cast<void*>(&D##Name), Slot(&g_##Name)}

bool InstallConstructors(registry::Mode)
{
    const Detour detours[] = {
        OPENSC4_D(ConstructBreak, addr::HTMLBreakNode_Construct),
        OPENSC4_D(ConstructRule, addr::HTMLRuleNode_Construct),
        OPENSC4_D(ConstructInput, addr::HTMLInputNode_Construct),
        OPENSC4_D(ConstructContainer, addr::HTMLContainerNode_Construct),
        OPENSC4_D(ConstructArea, addr::HTMLAreaNode_Construct),
        OPENSC4_D(ConstructBlink, addr::HTMLBlinkNode_Construct),
        OPENSC4_D(ConstructQuote, addr::HTMLQuoteNode_Construct),
        OPENSC4_D(ConstructDD, addr::HTMLDDNode_Construct),
        OPENSC4_D(ConstructAlign, addr::HTMLAlignNode_Construct),
        OPENSC4_D(ConstructDT, addr::HTMLDTNode_Construct),
        OPENSC4_D(ConstructForm, addr::HTMLFormNode_Construct),
        OPENSC4_D(ConstructHeading, addr::HTMLHeadingNode_Construct),
        OPENSC4_D(ConstructItem, addr::HTMLItemNode_Construct),
        OPENSC4_D(ConstructItemBody, addr::HTMLItemBodyNode_Construct),
        OPENSC4_D(ConstructMap, addr::HTMLMapNode_Construct),
        OPENSC4_D(ConstructOption, addr::HTMLOptionNode_Construct),
        OPENSC4_D(ConstructParagraph, addr::HTMLParagraphNode_Construct),
        OPENSC4_D(ConstructSelect, addr::HTMLSelectNode_Construct),
        OPENSC4_D(ConstructSpan, addr::HTMLSpanNode_Construct),
        OPENSC4_D(ConstructRow, addr::HTMLRowNode_Construct),
        OPENSC4_D(ConstructHeaderCell, addr::HTMLHeaderCellNode_Construct),
    };
    return InstallAll("constructors", detours);
}

bool InstallDestructors(registry::Mode)
{
    const Detour detours[] = {
        OPENSC4_D(DeleteLeaf, addr::HTMLNode_Delete),
        OPENSC4_D(DestroyRow, addr::HTMLRowNode_Destruct),
        OPENSC4_D(DeleteRow, addr::HTMLRowNode_Delete),
    };
    return InstallAll("destructors", detours);
}

bool InstallAttributes(registry::Mode)
{
    const Detour detours[] = {
        OPENSC4_D(SetTableBackground, addr::HTMLTableNode_SetBackground),
        OPENSC4_D(SetNodeBackground, addr::HTMLNode_SetBackground),
        OPENSC4_D(ParseAlign, addr::HTMLText_ParseAlign),
        OPENSC4_D(ParseImageAlign, addr::HTMLText_ParseImageAlign),
        OPENSC4_D(SetHref, addr::HTMLLinkNode_SetHref),
        OPENSC4_D(SetAddress, addr::HTMLLinkNode_SetAddress),
        OPENSC4_D(SetName, addr::HTMLLinkNode_SetName),
        OPENSC4_D(SetNoWrap, addr::HTMLCellNode_SetNoWrap),
        OPENSC4_D(SetString28, addr::HTMLMapNode_SetName),
        OPENSC4_D(SetMetaName, addr::HTMLMetaNode_SetName),
        OPENSC4_D(SetMetaContent, addr::HTMLMetaNode_SetContent),
        OPENSC4_D(SetMetaHttpEquiv, addr::HTMLMetaNode_SetHttpEquiv),
        OPENSC4_D(CheckAttributes, addr::HTMLDocument_CheckAttributes),
    };
    return InstallAll("attributes", detours);
}

bool InstallHandlers(registry::Mode)
{
    const Detour detours[] = {
        OPENSC4_D(ParseBreak, addr::HTMLDocument_ParseBR),
        OPENSC4_D(ParseInput, addr::HTMLDocument_ParseInput),
        OPENSC4_D(ParseArea, addr::HTMLDocument_ParseArea),
        OPENSC4_D(ParseBase, addr::HTMLDocument_ParseBase),
        OPENSC4_D(ParseRule, addr::HTMLDocument_ParseHR),
        OPENSC4_D(ParseImage, addr::HTMLDocument_ParseIMG),
        OPENSC4_D(ParseHeadLink, addr::HTMLDocument_ParseLinkTag),
        OPENSC4_D(ParseMeta, addr::HTMLDocument_ParseMeta),
    };
    return InstallAll("handlers", detours);
}

bool InstallTokenizer(registry::Mode)
{
    const Detour detours[] = {
        OPENSC4_D(Hash, addr::HTMLStringHash_Hash),
        OPENSC4_D(BucketOf, addr::HTMLStringHash_Bucket),
        OPENSC4_D(Find, addr::HTMLTagTable_Find),
        OPENSC4_D(DeleteMapNode, addr::HTMLAttributes_DeleteNode),
        OPENSC4_D(EraseNode, addr::HTMLAttributes_EraseNode),
        OPENSC4_D(EraseAt, addr::HTMLAttributes_Erase),
        OPENSC4_D(Clear, addr::HTMLAttributes_Clear),
        OPENSC4_D(GetAttribute, addr::HTMLDocument_GetAttribute),
        OPENSC4_D(Trim, addr::HTMLText_Trim),
        OPENSC4_D(SplitTagName, addr::HTMLDocument_SplitTagName),
        OPENSC4_D(SkipSpaces, addr::HTMLDocument_SkipSpaces),
    };
    return InstallAll("tokenizer", detours);
}

const bool g_registered[] = {
    registry::Register({"ui.html.document.ctors", "ui/html", registry::Kind::Detour, registry::Phase::Attach,
                        addr::HTMLContainerNode_Construct,
                        "Our HTML nodes' constructors outside h5: BR, HR, INPUT, the containers' base and the 17 "
                        "classes on it (21 detours; docs/html.md)",
                        false, InstallConstructors}),
    registry::Register({"ui.html.document.dtors", "ui/html", registry::Kind::Detour, registry::Phase::Attach,
                        addr::HTMLNode_Delete,
                        "Our HTML leaves' deleting destructor and TR's destructors (3 detours; docs/html.md)", false,
                        InstallDestructors}),
    registry::Register({"ui.html.document.attributes", "ui/html", registry::Kind::Detour, registry::Phase::Attach,
                        addr::HTMLDocument_CheckAttributes,
                        "Our HTML attribute setters: BGCOLOR, ALIGN, A's strings, NOWRAP, ALT, META's strings and "
                        "the attribute check (13 detours; docs/html.md)",
                        false, InstallAttributes}),
    registry::Register({"ui.html.document.handlers", "ui/html", registry::Kind::Detour, registry::Phase::Attach,
                        addr::HTMLDocument_ParseIMG,
                        "Our HTML small tag handlers: BR, INPUT, AREA, BASE, HR, IMG, LINK, META (8 detours; "
                        "docs/html.md)",
                        false, InstallHandlers}),
    registry::Register({"ui.html.document.tokenizer", "ui/html", registry::Kind::Detour, registry::Phase::Attach,
                        addr::HTMLDocument_GetAttribute,
                        "Our HTML tokenizer helpers: the tag's name, trimming, spaces, the hash maps' hash, find, "
                        "erase and clear, GetAttribute (11 detours; docs/html.md)",
                        false, InstallTokenizer}),
};

} // namespace
} // namespace opensc4::html_document
