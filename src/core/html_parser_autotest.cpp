// The HTML parser's part of the autotest (docs/html.md, "Testing"):
// OPENSC4_TEST_HTML_PARSER=<day> parses pages with no window on screen and
// hashes the node tree before layout. The pages: our own, written to reach
// every tag handler and the tokenizer's edge cases, and the rows the news
// ticker makes from every advice of the city (a ticker of the test's own,
// as advisor_autotest.cpp's ticker test makes one, its lists caught by a
// stand-in). Each page goes through a document of its own, as the HTML
// window's ShowHTML makes one: the document's constructor, its Init with a
// window of the test's own (made by the class factory, never created on
// screen) and a stand-in Internet service that takes image requests and does
// nothing, then its Parse (slot 8).
//
// With any ui.html.parser group ours, each page is parsed twice, through the
// game's functions (html_parser::g_useGame) and through ours, and the two
// trees' hashes compared (`autotest: html parser: page <n> differs` for a
// difference). The fingerprint's `hp` part is the hash of ours (the game's
// when no group is ours), so runs with the groups off and on must give the
// same. Also hashed: the static tag table, bucket by bucket.
#include "core/autotest_hooks.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace opensc4::html_parser {
extern bool g_useGame;
}

namespace opensc4::autotest {
namespace {

struct Fnv {
    uint32_t h = 2166136261u;
    void Byte(uint8_t value) { h = (h ^ value) * 16777619u; }
    void Word(uint32_t value)
    {
        for (int i = 0; i < 4; ++i)
            Byte(static_cast<uint8_t>(value >> (8 * i)));
    }
    void Bytes(const char* first, const char* last)
    {
        if (first && first <= last && last - first < 0x100000)
            for (const char* at = first; at != last; ++at)
                Byte(static_cast<uint8_t>(*at));
        Word(first && first <= last ? static_cast<uint32_t>(last - first) : 0xFFFFFFFFu);
    }
};


// ====================================================================================
// The tree's hash

enum class Field : uint8_t {
    Word,   // a dword as it is
    Byte,   // a byte
    Body,   // an STL string body (begin, end, storage): its text
    Object, // a pointer to an object of its own: null or not, and its vtable
    Vector, // begin, end, storage: the number of bytes
    TextPtr, // a pointer to a game string of its own (or null): its text
};

struct FieldAt {
    uint16_t offset;
    Field kind;
};

struct NodeClass {
    uint32_t vtable;
    bool container; // children at +0x1C
    std::vector<FieldAt> fields;
};

// The node classes and what is hashed of each beyond the base (kind at +4,
// two bytes at +8, the box at +0xC) and the children. Fields the game leaves
// uninitialised until a handler sets them are not listed.
const std::vector<NodeClass>& Classes()
{
    static const std::vector<NodeClass> classes = {
        {0x00ACD504, false, {}},                                                                  // a leaf
        {0x00ACD530, false, {}},                                                                  // BR
        {0x00ACD55C, false, {{0x1C, Field::Word}, {0x20, Field::Word}, {0x24, Field::Word}, {0x28, Field::Byte}}}, // HR
        {0x00ACD588, false, {}},                                                                  // INPUT
        {0x00ACD5E8, true, {}},                                                                   // a container
        {0x00ACD614, true, {{0x28, Field::Word}}},                                                // AREA
        {0x00ACD640, true, {}},                                                                   // BLINK
        {0x00ACD66C, true, {}},                                                                   // BLOCKQUOTE
        {0x00ACD698, true, {}},                                                                   // DT
        {0x00ACD6C4, true, {}},                                                                   // CENTER, DIV
        {0x00ACD6F0, true, {}},                                                                   // DD
        {0x00ACD71C, true, {}},                                                                   // FORM
        {0x00ACD748, true, {}},                                                                   // H1 to H6
        {0x00ACD774, true, {}},                                                                   // LI
        {0x00ACD7A0, true, {}},                                                                   // LI (numbered)
        {0x00ACD7CC, true, {{0x28, Field::Word}}},                                                // MAP
        {0x00ACD7F8, true, {}},                                                                   // OPTION
        {0x00ACD824, true, {}},                                                                   // P
        {0x00ACD850, true, {}},                                                                   // SELECT
        {0x00ACD87C, true, {}},                                                                   // SPAN
        {0x00ACD8A8, true, {{0x28, Field::Vector}, {0x38, Field::Byte}}}, // TR
        {0x00ACD8D4, true, {}},                                                                   // TH
        {0x00ACE20C, true, {{0x28, Field::Word}}},                                                // UL, OL
        {0x00ACE238, true, {{0x28, Field::Word}, {0x2C, Field::Word}, {0x34, Field::Word}, {0x3C, Field::Word},
                            {0x48, Field::Byte}, {0x70, Field::Word}, {0x74, Field::Word}, {0x78, Field::Word},
                            {0x7C, Field::Vector}, {0x88, Field::Vector}}},                       // TABLE
        {0x00ACE264, true, {{0x28, Field::Word}, {0x2C, Field::Word}, {0x38, Field::Byte}, {0x39, Field::Byte}}}, // TD
        {0x00ACE290, true, {{0x28, Field::TextPtr}, {0x2C, Field::TextPtr}, {0x30, Field::TextPtr}}}, // A
        {0x00ACE2C0, true, {{0x28, Field::Word}, {0x2C, Field::Word}, {0x30, Field::Word}, {0x34, Field::Word},
                            {0x38, Field::Word}, {0x3C, Field::Word}, {0x40, Field::Byte}}},      // the body
        {0x00ACE300, false, {{0x1C, Field::Word}, {0x24, Field::Body}, {0x34, Field::Word}, {0x38, Field::Word},
                             {0x3C, Field::Byte}, {0x3D, Field::Byte}, {0x40, Field::Word}, {0x44, Field::Word},
                             {0x48, Field::Word}, {0x4C, Field::Word}, {0x50, Field::Word}, {0x54, Field::Word}}}, // text
        {0x00ACE188, false, {{0x1C, Field::Word}, {0x20, Field::Word}, {0x24, Field::Word}, {0x28, Field::Word},
                             {0x2C, Field::Word}, {0x30, Field::Word}, {0x34, Field::Word}}},     // IMG
        {0x00ACE1B4, false, {{0x1C, Field::Word}, {0x20, Field::Byte}, {0x24, Field::Word}}},    // LINK
        {0x00ACE1E0, false, {{0x1C, Field::Word}, {0x20, Field::Word}, {0x24, Field::Word}}},    // META
    };
    return classes;
}

const NodeClass* ClassOf(uint32_t vtable)
{
    for (const NodeClass& c : Classes())
        if (c.vtable == vtable)
            return &c;
    return nullptr;
}

// While a page that differs is parsed again, every value hashed is also
// written here, a node to a line.
std::string* g_trace = nullptr;

void Trace(const char* format, uint32_t value)
{
    if (!g_trace)
        return;
    char text[32];
    std::snprintf(text, sizeof text, format, value);
    *g_trace += text;
}

// A dword as it is, unless it points into the heap: then what it points to
// (the colours of the font stack and the body are pointers to a colour).
// Heap addresses change from run to run; small numbers, the game's image
// and colours with their alpha set do not look like them.
void HashWord(Fnv& f, uint32_t value)
{
    if (value >= 0x01000000u && value < 0x80000000u) {
        MEMORY_BASIC_INFORMATION info{};
        if (VirtualQuery(reinterpret_cast<void*>(static_cast<uintptr_t>(value)), &info, sizeof info) &&
            info.State == MEM_COMMIT && (info.Protect & (PAGE_READWRITE | PAGE_READONLY)) && !(info.Protect & PAGE_GUARD)) {
            const uint32_t pointed = *reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(value));
            f.Byte(0xEE);
            f.Word(pointed);
            Trace(" *%X", pointed);
            return;
        }
    }
    f.Word(value);
    Trace(" %X", value);
}

void HashField(Fnv& f, char* object, const FieldAt& field)
{
    switch (field.kind) {
    case Field::Word:
        HashWord(f, At<uint32_t>(object, field.offset));
        break;
    case Field::Byte:
        f.Byte(At<uint8_t>(object, field.offset));
        Trace(" b%X", At<uint8_t>(object, field.offset));
        break;
    case Field::Body: {
        const char* const first = At<char*>(object, field.offset);
        const char* const last = At<char*>(object, field.offset + 4);
        f.Bytes(first, last);
        if (g_trace && first && first <= last && last - first < 200) {
            *g_trace += " \"";
            g_trace->append(first, last);
            *g_trace += "\"";
        }
        break;
    }
    case Field::Object: {
        char* const p = At<char*>(object, field.offset);
        f.Byte(p != nullptr);
        if (p)
            f.Word(At<uint32_t>(p, 0));
        Trace(" o%X", p ? At<uint32_t>(p, 0) : 0);
        break;
    }
    case Field::TextPtr: {
        char* const p = At<char*>(object, field.offset);
        f.Byte(p != nullptr);
        Trace(p ? " t" : " t0", 0);
        if (p)
            HashField(f, p, {0x4, Field::Body});
        break;
    }
    case Field::Vector: {
        const uint32_t bytes =
            static_cast<uint32_t>(At<char*>(object, field.offset + 4) - At<char*>(object, field.offset));
        f.Word(bytes);
        Trace(" v%X", bytes);
        break;
    }
    }
}

int g_nodes = 0;
int g_unknown = 0;

void HashNode(Fnv& f, char* node, int depth)
{
    f.Byte(0xA5);
    if (g_trace)
        *g_trace += "\n" + std::string(static_cast<std::size_t>(depth) * 2, ' ');
    if (!node || depth > 64) {
        f.Byte(0);
        Trace("null", 0);
        return;
    }
    ++g_nodes;
    const uint32_t vtable = At<uint32_t>(node, 0);
    f.Word(vtable);
    Trace("%08X", vtable);
    for (std::size_t at = 0x4; at < 0x8; at += 4)
        HashWord(f, At<uint32_t>(node, at));
    f.Byte(At<uint8_t>(node, 0x8));
    f.Byte(At<uint8_t>(node, 0x9));
    Trace(" b%X", At<uint8_t>(node, 0x8));
    Trace(" b%X |", At<uint8_t>(node, 0x9));
    for (std::size_t at = 0xC; at < 0x1C; at += 4)
        HashWord(f, At<uint32_t>(node, at));
    Trace(" |", 0);
    const NodeClass* const c = ClassOf(vtable);
    if (!c) {
        ++g_unknown;
        return;
    }
    for (const FieldAt& field : c->fields)
        HashField(f, node, field);
    if (c->container) {
        char** const first = At<char**>(node, 0x1C);
        char** const last = At<char**>(node, 0x20);
        const uint32_t count = first && first <= last ? static_cast<uint32_t>(last - first) : 0;
        f.Word(count);
        for (uint32_t i = 0; i < count && i < 100000; ++i)
            HashNode(f, first[i], depth + 1);
    }
}

// The document's own fields after parsing: the tokenizer's state, the font
// stack's depth, the lists it keeps and the root.
void HashDocument(Fnv& f, char* doc)
{
    Trace("doc", 0);
    f.Byte(At<uint8_t>(doc, 0x18));
    for (std::size_t at = 0x1C; at <= 0x24; at += 4)
        HashWord(f, At<uint32_t>(doc, at));
    for (std::size_t at = 0x28; at <= 0x2A; ++at)
        f.Byte(At<uint8_t>(doc, at));
    for (const std::size_t at : {0x44u, 0x5Cu, 0x74u, 0x88u, 0x9Cu, 0xD8u, 0xF0u, 0x104u, 0x134u, 0x184u})
        HashField(f, doc, {static_cast<uint16_t>(at), Field::Body});
    for (const std::size_t at : {0xACu, 0xBCu, 0xC8u, 0x15Cu, 0x168u, 0x174u})
        HashField(f, doc, {static_cast<uint16_t>(at), Field::Vector});
    for (const std::size_t at : {0xE8u, 0xCu, 0x124u, 0x128u, 0x12Cu, 0x144u, 0x148u, 0x14Cu, 0x150u, 0x158u, 0x194u})
        HashWord(f, At<uint32_t>(doc, at));
    f.Byte(At<uint8_t>(doc, 0x154));
    f.Byte(At<uint8_t>(doc, 0x155));
    HashNode(f, At<char*>(doc, 0x14), 0);
}

// The document's entity map (a tree at +8), in order: name and code.
void HashEntities(Fnv& f, char* doc)
{
    char* const header = At<char*>(doc, 0x8);
    if (!header)
        return;
    int count = 0;
    char* node = At<char*>(header, 0x8); // leftmost
    while (node && node != header && count < 1000) {
        f.Bytes(At<char*>(node, 0x14), At<char*>(node, 0x18));
        f.Word(At<uint32_t>(node, 0x24));
        ++count;
        if (char* right = At<char*>(node, 0xC)) {
            while (At<char*>(right, 0x8))
                right = At<char*>(right, 0x8);
            node = right;
        } else {
            char* parent = At<char*>(node, 0x4);
            while (node == At<char*>(parent, 0xC)) {
                node = parent;
                parent = At<char*>(parent, 0x4);
            }
            if (At<char*>(node, 0xC) != parent)
                node = parent;
        }
    }
    f.Word(static_cast<uint32_t>(count));
}

// The static tag table, bucket by bucket: each chain's names and IDs.
uint32_t HashTagTable(int& entries)
{
    Fnv f;
    char* const table = reinterpret_cast<char*>(addr::HTMLTags_Table);
    char** const first = At<char**>(table, 0x4);
    char** const last = At<char**>(table, 0x8);
    const uint32_t buckets = first && first <= last ? static_cast<uint32_t>(last - first) : 0;
    f.Word(buckets);
    f.Word(At<uint32_t>(table, 0x10));
    entries = 0;
    for (uint32_t i = 0; i < buckets && i < 100000; ++i) {
        f.Byte(0xB0);
        for (char* node = first[i]; node && entries < 100000; node = At<char*>(node, 0x0)) {
            f.Bytes(At<char*>(node, 0x8), At<char*>(node, 0xC));
            f.Word(At<uint32_t>(node, 0x18));
            ++entries;
        }
    }
    return f.h;
}

// ====================================================================================
// A document with no window on screen

// The game's string (cRZString's layout).
struct GameText {
    uint32_t vtable;
    char* begin;
    char* end;
    char* storage;
    uint32_t spare;
};

void MakeText(GameText* text, const char* from)
{
    reinterpret_cast<GameText*(__thiscall*)(GameText*, const char*)>(addr::SC4String_FromText)(text, from);
}

void DestroyText(GameText* text) { reinterpret_cast<void(__thiscall*)(GameText*)>(addr::SC4String_Destroy)(text); }

// A stand-in Internet service: reference counting, slot 17 (a request:
// address, the document's +4 part, kind 3 and two flags) that the image
// nodes and the body's background call, and slot 22 (a request
// cancelled) that the document's release calls for each image still
// waiting, both doing nothing, and slot 24 (whether a link's address was
// visited) that the links ask, always no. Any other slot ends the run.
struct Service {
    void** vtable;
};
int g_serviceRequests = 0;

uint8_t __fastcall ServiceQuery(Service*, void*, uint32_t, void** out)
{
    if (out)
        *out = nullptr;
    return 0;
}
uint32_t __fastcall ServiceRef(Service*, void*) { return 1; }
uint8_t __fastcall ServiceRequest(Service*, void*, void*, void*, uint32_t, uint32_t, uint32_t)
{
    ++g_serviceRequests;
    return 0;
}
uint8_t __fastcall ServiceCancel(Service*, void*, uint32_t) { return 1; }
uint8_t __fastcall ServiceVisited(Service*, void*, const void*) { return 0; }

template <int Slot>
uint32_t __fastcall ServiceOther(Service*, void*)
{
    // Its arguments are unknown, so the run cannot go on.
    log::Write("autotest: html parser: the stand-in service's slot %d was called; stopping", Slot);
    TerminateProcess(GetCurrentProcess(), 3);
    return 0;
}

template <int... Slots>
void FillService(void** vtable, std::integer_sequence<int, Slots...>)
{
    ((vtable[Slots] = reinterpret_cast<void*>(&ServiceOther<Slots>)), ...);
}

void* g_serviceVtable[32] = {};
Service g_service{g_serviceVtable};

void SetUpService()
{
    FillService(g_serviceVtable, std::make_integer_sequence<int, 32>{});
    g_serviceVtable[0] = reinterpret_cast<void*>(&ServiceQuery);
    g_serviceVtable[1] = reinterpret_cast<void*>(&ServiceRef);
    g_serviceVtable[2] = reinterpret_cast<void*>(&ServiceRef);
    g_serviceVtable[17] = reinterpret_cast<void*>(&ServiceRequest);
    g_serviceVtable[22] = reinterpret_cast<void*>(&ServiceCancel);
    g_serviceVtable[24] = reinterpret_cast<void*>(&ServiceVisited);
}

// The attribute map (a hash map at document +0x114), bucket by bucket:
// each name and value.
void HashAttributes(Fnv& f, char* doc)
{
    char* const map = doc + 0x114;
    char** const first = At<char**>(map, 0x4);
    char** const last = At<char**>(map, 0x8);
    const uint32_t buckets = first && first <= last ? static_cast<uint32_t>(last - first) : 0;
    f.Word(buckets);
    HashWord(f, At<uint32_t>(map, 0x10));
    int count = 0;
    for (uint32_t i = 0; i < buckets && i < 10000; ++i)
        for (char* node = first[i]; node && count < 10000; node = At<char*>(node, 0x0), ++count) {
            Trace(" [", 0);
            HashField(f, node, {0x8, Field::Body});
            HashField(f, node, {0x1C, Field::Body});
            Trace("]", 0);
        }
}

// The tokens of a page: NextTag over it in a document of its own (the page
// put in place, nothing parsed), asking for text every other time, each
// token's ID, text, name, rest and attributes hashed.
uint32_t TokenPage(const std::string& page)
{
    using NewFn = void*(__cdecl*)(std::size_t);
    using ThisFn = void*(__thiscall*)(void*);
    char* const doc = static_cast<char*>(reinterpret_cast<NewFn>(addr::SC4_operator_new)(0x1C4));
    if (!doc)
        return 0;
    reinterpret_cast<ThisFn>(addr::HTMLDocument_Construct)(doc);
    Virtual<uint32_t>(doc, 0x4);
    GameText text;
    MakeText(&text, page.c_str());
    reinterpret_cast<void(__thiscall*)(void*, const GameText*)>(addr::cRZBaseString_Assign)(doc + 0x180, &text);
    DestroyText(&text);
    At<uint32_t>(doc, 0x194) = 0;
    Fnv f;
    for (int i = 0; i < 400; ++i) {
        reinterpret_cast<void(__thiscall*)(void*, uint8_t)>(addr::HTMLDocument_NextTag)(doc, i % 2);
        Trace("\n%X", At<uint32_t>(doc, 0xE8));
        HashWord(f, At<uint32_t>(doc, 0xE8));
        HashWord(f, At<uint32_t>(doc, 0x194));
        for (const std::size_t at : {0xD8u, 0xF0u, 0x104u})
            HashField(f, doc, {static_cast<uint16_t>(at), Field::Body});
        HashAttributes(f, doc);
        if (At<uint32_t>(doc, 0xE8) == 1)
            break;
    }
    Virtual<uint32_t>(doc, 0x8);
    return f.h;
}

// One page parsed in a document of its own; the hash of what it left.
uint32_t ParsePage(char* window, const std::string& page, bool& parsed)
{
    using NewFn = void*(__cdecl*)(std::size_t);
    using ThisFn = void*(__thiscall*)(void*);
    using InitFn = uint8_t(__thiscall*)(void*, void*, void*, const GameText*);
    char* const doc = static_cast<char*>(reinterpret_cast<NewFn>(addr::SC4_operator_new)(0x1C4));
    if (!doc)
        return 0;
    reinterpret_cast<ThisFn>(addr::HTMLDocument_Construct)(doc);
    Virtual<uint32_t>(doc, 0x4); // AddRef
    GameText base;
    static const std::string baseText = Env("OPENSC4_TEST_HTML_PARSER_BASE");
    MakeText(&base, baseText.c_str());
    reinterpret_cast<InitFn>(addr::HTMLDocument_Init)(doc, window, &g_service, &base);
    DestroyText(&base);
    GameText text;
    MakeText(&text, page.c_str());
    if (EnvInt("OPENSC4_TEST_HTML_PARSER_DUMP", 0))
        log::Write("autotest: html parser: parsing");
    parsed = Virtual<uint8_t>(doc, 0x20, &text) != 0;
    DestroyText(&text);
    Fnv f;
    f.Byte(parsed ? 1 : 0);
    HashDocument(f, doc);
    HashEntities(f, doc);
    if (EnvInt("OPENSC4_TEST_HTML_PARSER_DUMP", 0))
        log::Write("autotest: html parser: parsed and hashed, releasing");
    Virtual<uint32_t>(doc, 0x8); // Release: the document and its tree go
    return f.h;
}

// ====================================================================================
// The pages

// Ours, one feature or edge case after another.
const char* const kPages[] = {
    // Plain text, no tags; an empty page; a lone `<`; an unclosed tag.
    "Just some text, no tags at all.",
    "",
    "<",
    "<HTML><BODY>never closed <B",
    // A whole page with a DOCTYPE, a head and the body's attributes.
    "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.0 Transitional//EN\">\n<HTML><HEAD><TITLE>Title &amp; more</TITLE>"
    "<META NAME=\"x\" CONTENT=\"y\"><LINK REL=stylesheet HREF=\"a.css\"></HEAD>\n"
    "<BODY BGCOLOR=\"#102030\" TEXT=\"#FFFFFF\" LINK=red VLINK=\"blue\" ALINK=#00ff00 BACKGROUND=\"bg.png\">"
    "Hello</BODY></HTML>",
    // Fonts and phrase tags, nested and badly nested.
    "<HTML><BODY><FONT FACE=\"Arial\" SIZE=4 COLOR=\"#FF0000\">red <B>bold <I>both</B> italic</I></FONT> "
    "<TT>tt</TT><U>u</U><STRIKE>s</STRIKE><BIG>big</BIG><SMALL>small</SMALL><EM>em</EM><STRONG>strong</STRONG>"
    "<DFN>d</DFN><CODE>c</CODE><SAMP>s</SAMP><KBD>k</KBD><VAR>v</VAR><CITE>c</CITE><ABBR>a</ABBR>"
    "<ACRONYM>a</ACRONYM><FONT SIZE=+2>plus</FONT><FONT SIZE=-1>minus</FONT><FONT COLOR=navy>navy</FONT>"
    "</BODY></HTML>",
    // Entities: named, unknown, numeric, at the end.
    "<HTML><BODY>&quot;&amp;&lt;&gt;&nbsp;&copy;&reg;&eacute;&Uuml;&yuml;&bogus;&#65;&#x42;&#;&amp</BODY></HTML>",
    // Comments, closed and not.
    "<HTML><BODY>a<!-- one -->b<!---->c<!-- <B>not a tag</B> -->d<!-- never closed</BODY></HTML>",
    // Attributes: quotes of both kinds, empty, bare, spaced, in quotes, odd names.
    "<HTML><BODY><A HREF=\"x.html\" NAME='n' TITLE=\"\" DATA = 'y' \"Q\"=1 bare-name/ x=unquoted"
    " -dash=z>link</A><A HREF=sc4://cmd?a=b&c=d>sc4</A><FONT COLOR=\"#12AB34\" COLOR=\"#000000\">dup</FONT>"
    "<SPAN STYLE=\"color: #102030; text-decoration: underline; font-size: 12pt; font-family: arial\">span</SPAN>"
    "<SPAN STYLE=\"color: rgb(1,2,3)\">rgb</SPAN></BODY></HTML>",
    // Blocks.
    "<HTML><BODY><P>para<P ALIGN=center>two</P><BR><BR CLEAR=all><HR><HR SIZE=4 WIDTH=50% NOSHADE>"
    "<HR WIDTH=120><CENTER>c</CENTER><DIV ALIGN=right>r</DIV><DIV ALIGN=left>l</DIV><BLOCKQUOTE>q</BLOCKQUOTE>"
    "<H1>h1</H1><H2>h2</H2><H3 ALIGN=center>h3</H3><H4>h4</H4><H5>h5</H5><H6>h6</H6><PRE>  pre\n  text</PRE>"
    "<NOSCRIPT>skipped<B>x</B></NOSCRIPT><ADDRESS>a</ADDRESS><FIELDSET>f</FIELDSET><DIR>d</DIR><MENU>m</MENU>"
    "</BODY></HTML>",
    // Lists.
    "<HTML><BODY><UL><LI>one<LI>two<UL TYPE=square><LI>inner</UL></UL><OL><LI>1<LI>2<LI VALUE=7>7</OL>"
    "<OL START=3 TYPE=a><LI>c</LI></OL><DL><DT>term<DD>def<DT>t2</DL></BODY></HTML>",
    // Tables.
    "<HTML><BODY><TABLE BORDER=1 WIDTH=\"100%\" BGCOLOR=\"#C0C0C0\" CELLPADDING=3 CELLSPACING=2>"
    "<CAPTION>cap</CAPTION><THEAD><TR><TH>h</TH><TH WIDTH=40>h2</TH></TR></THEAD><TBODY>"
    "<TR BGCOLOR=yellow VALIGN=top><TD>a</TD><TD VALIGN=bottom WIDTH=\"30%\" BGCOLOR=\"#0000FF\" NOWRAP>b</TD></TR>"
    "<TR><TD COLSPAN=2 ROWSPAN=1>c<TABLE><TR><TD>nested</TD></TR></TABLE></TD></TBODY><TFOOT><TR><TD>f</TD></TR>"
    "</TFOOT></TABLE><TABLE WIDTH=200><TR><TD>w</TABLE></BODY></HTML>",
    // Links, maps, spans, blink, scripts and skipped inline tags.
    "<HTML><BODY><A HREF=\"page.html\">to page</A><A NAME=\"anchor\">here</A><MAP NAME=m><AREA SHAPE=RECT "
    "COORDS=\"0,0,10,10\" HREF=a.html><AREA SHAPE=circle COORDS=\"5,5,5\" HREF=b.html><AREA SHAPE=poly "
    "COORDS=\"0,0,5,5,0,5\"><AREA SHAPE=default></MAP><BLINK>blink</BLINK><SCRIPT>var x = '<B>';</SCRIPT>"
    "<OBJECT>o</OBJECT><Q>q</Q><SUB>sub</SUB><SUP>sup</SUP><BDO>b</BDO><NOBR>no break</NOBR></BODY></HTML>",
    // Forms.
    "<HTML><BODY><FORM ACTION=x><INPUT TYPE=text NAME=a VALUE=b><SELECT NAME=s><OPTION>one<OPTION SELECTED>two"
    "</SELECT><TEXTAREA>t</TEXTAREA><LABEL>l</LABEL><BUTTON>b</BUTTON></FORM></BODY></HTML>",
    // Images.
    "<HTML><BODY><IMG SRC=\"a.png\"><IMG SRC=b.jpg ALT=\"bee\" WIDTH=10 HEIGHT=20 ALIGN=left>"
    "<IMG SRC=\"c.png\" ALIGN=right BORDER=0><IMG></BODY></HTML>",
    // Unknown tags, lower case, end tags without start tags, text after </HTML>.
    "<html><body><unknown attr=1>u</unknown><b>lower</b></p></td></tr></table></BODY></HTML>after",
    // No <HTML>, a body straight away.
    "<BODY>body only<P>p</BODY>",
    // A DOCTYPE without an HTML tag after it.
    "<!DOCTYPE HTML>text",
};

// The rows of a news ticker of our own: every advice of the city added,
// the news list's rows and the ticker's caught by a stand-in list.
std::vector<std::string>* g_rows = nullptr;

struct RowText {
    std::uintptr_t vtable;
    char* begin;
    char* end;
};

void KeepRow(const RowText* text)
{
    if (g_rows && text && text->begin && text->begin <= text->end && text->end - text->begin < 0x10000)
        g_rows->emplace_back(text->begin, text->end);
}

void __fastcall RowAdd(void*, void*, const RowText* image1, const RowText* image2, const RowText* message,
                       const RowText* title, uint32_t, char*, const RowText* image3, uint32_t, uint32_t)
{
    KeepRow(title);
    KeepRow(message);
    std::string images;
    for (const RowText* image : {image1, image2, image3})
        if (image && image->begin && image->begin <= image->end)
            images.append(image->begin, image->end);
    if (g_rows && !images.empty())
        g_rows->push_back(images);
}

void __fastcall RowOne(void*, void*, uint32_t) {}
void __fastcall RowClear(void*, void*) {}

void* g_rowListVtable[8] = {nullptr,
                            nullptr,
                            nullptr,
                            nullptr,
                            reinterpret_cast<void*>(&RowAdd),
                            reinterpret_cast<void*>(&RowOne),
                            reinterpret_cast<void*>(&RowOne),
                            reinterpret_cast<void*>(&RowClear)};
void* g_rowList = g_rowListVtable;

std::vector<char*> AllAdvices(char* system)
{
    std::vector<char*> advices;
    char* const header = At<char*>(system, 0x4C);
    for (char* node = At<char*>(header, 0x8); node != header;) {
        char* const advisor = At<char*>(node, 0x14) - 0x8;
        char* const first = At<char*>(advisor, 0x84);
        char* const last = At<char*>(advisor, 0x88);
        const bool sane = first <= last && (last - first) % 0x10 == 0 && last - first < 0x10000;
        for (char* record = first; sane && record != last; record += 0x10)
            if (char* const advice = At<char*>(record, 0x0))
                advices.push_back(advice);
        if (char* right = At<char*>(node, 0xC)) {
            while (At<char*>(right, 0x8))
                right = At<char*>(right, 0x8);
            node = right;
        } else {
            char* parent = At<char*>(node, 0x4);
            while (node == At<char*>(parent, 0xC)) {
                node = parent;
                parent = At<char*>(parent, 0x4);
            }
            if (At<char*>(node, 0xC) != parent)
                node = parent;
        }
    }
    return advices;
}

std::vector<std::string> TickerRows()
{
    std::vector<std::string> rows;
    char* const system = Global<char*>(addr::cSC4AdvisorSystem_Instance);
    if (!system)
        return rows;
    using NewFn = void*(__cdecl*)(std::size_t);
    char* const memory = static_cast<char*>(reinterpret_cast<NewFn>(addr::SC4_operator_new)(0x84));
    if (!memory)
        return rows;
    char* const ticker = reinterpret_cast<char*(__thiscall*)(void*)>(addr::SC4UI_F773050)(memory);
    Virtual<uint32_t>(ticker, 0x4);
    At<uint32_t>(ticker, 0x80) = 12;
    for (char* const advice : AllAdvices(system))
        Virtual<void>(ticker, 0x20, advice); // Add
    g_rows = &rows;
    At<void*>(ticker, 0x38) = &g_rowList;
    Virtual<void>(ticker, 0x24); // the news list's rows
    At<void*>(ticker, 0x38) = nullptr;
    At<void*>(ticker, 0x3C) = &g_rowList;
    for (int turn = 0; turn < 4; ++turn)
        Virtual<void>(ticker, 0x28); // the ticker moves on
    At<void*>(ticker, 0x3C) = nullptr;
    g_rows = nullptr;
    Virtual<uint32_t>(ticker, 0x8);
    return rows;
}

// ====================================================================================
// The test

bool g_on = false;
uint32_t g_hash = 2166136261u;

bool AnyOurs()
{
    for (const char* id : {"ui.html.parser.tokens", "ui.html.parser.tables"})
        if (registry::ModeOf(id) == registry::Mode::Ours)
            return true;
    return false;
}

void DriveParserDay(const Day& day)
{
    if (!OnRunDay(day, "OPENSC4_TEST_HTML_PARSER") || !game::IsBuild641())
        return;
    SetUpService();
    void* const face = reinterpret_cast<void*(__cdecl*)()>(addr::HTMLWindow_Create)();
    if (!face) {
        log::Write("autotest: html parser: no window");
        return;
    }
    Virtual<uint32_t>(face, 0x4);
    char* const window = static_cast<char*>(face) - 0xD8;

    std::vector<std::string> pages(std::begin(kPages), std::end(kPages));
    // OPENSC4_TEST_HTML_PARSER_FILE=<path>: the pages from a file instead,
    // separated by lines of `====` (for looking into one page).
    const std::string file = Env("OPENSC4_TEST_HTML_PARSER_FILE");
    if (!file.empty()) {
        pages.clear();
        if (std::FILE* in = std::fopen(file.c_str(), "rb")) {
            std::string all;
            char buffer[4096];
            for (std::size_t n; (n = std::fread(buffer, 1, sizeof buffer, in)) > 0;)
                all.append(buffer, n);
            std::fclose(in);
            for (std::size_t from = 0;;) {
                const std::size_t mark = all.find("\n====\n", from);
                pages.push_back(all.substr(from, mark == std::string::npos ? std::string::npos : mark - from));
                if (mark == std::string::npos)
                    break;
                from = mark + 6;
            }
        }
    }
    const std::size_t own = pages.size();
    // The ticker's rows, each one once.
    std::set<std::string> seen;
    for (std::string& row : TickerRows())
        if (seen.insert(row).second)
            pages.push_back(std::move(row));

    const bool compare = AnyOurs();
    Fnv total;
    int differ = 0;
    int parsedCount = 0;
    int nodes = 0;
    // Runs `run` on page i through the game's functions and through ours
    // (only ours, which are then the game's, when no group is ours) and
    // compares; a page that differs is run again on both sides with a trace.
    auto stage = [&](const char* name, std::size_t i, auto run) {
        uint32_t game = 0;
        if (compare) {
            html_parser::g_useGame = true;
            game = run();
            html_parser::g_useGame = false;
        }
        const uint32_t ours = run();
        total.Word(ours);
        if (compare && game != ours) {
            ++differ;
            log::Write("autotest: html parser: %s %zu differs: game %08X ours %08X (%zu bytes)", name, i, game, ours,
                       pages[i].size());
            if (differ <= 8) {
                std::string trace[2];
                for (int side = 0; side < 2; ++side) {
                    html_parser::g_useGame = side == 0;
                    g_trace = &trace[side];
                    run();
                    g_trace = nullptr;
                }
                html_parser::g_useGame = false;
                log::Write("autotest: html parser: %s %zu: %s\n  game:%s\n  ours:%s", name, i,
                           pages[i].c_str(), trace[0].c_str(), trace[1].c_str());
            }
        }
        return ours;
    };
    for (std::size_t i = 0; i < pages.size(); ++i) {
        if (EnvInt("OPENSC4_TEST_HTML_PARSER_DUMP", 0))
            log::Write("autotest: html parser: page %zu: %s", i, pages[i].c_str());
        const uint32_t tokens = stage("tokens", i, [&] { return TokenPage(pages[i]); });
        bool parsed = false;
        const uint32_t tree = stage("page", i, [&] {
            g_nodes = 0;
            const uint32_t h = ParsePage(window, pages[i], parsed);
            return h;
        });
        nodes += g_nodes;
        parsedCount += parsed ? 1 : 0;
        if (i < own || EnvInt("OPENSC4_TEST_HTML_PARSER_DUMP", 0))
            log::Write("autotest: html parser: page %zu tokens %08X tree %08X%s", i, tokens, tree,
                       parsed ? "" : " not parsed");
    }
    int entries = 0;
    const uint32_t table = HashTagTable(entries);
    total.Word(table);
    Virtual<uint32_t>(face, 0x8);

    g_hash = total.h;
    g_on = true;
    log::Write("autotest: html parser: %zu pages (%zu ours, %zu ticker rows), %d parsed, %d nodes, %d unknown "
               "nodes, %d image requests; tag table %08X (%d tags); %s; hash %08X",
               pages.size(), own, pages.size() - own, parsedCount, nodes, g_unknown, g_serviceRequests, table,
               entries, compare ? (differ ? "DIFFERENCES" : "game and ours the same") : "the game's only", g_hash);
    if (compare)
        log::Write("autotest: html parser: %d differences in %zu pages", differ, pages.size());
}

void ParserFingerprint(cISC4City*, std::string& line)
{
    if (!g_on)
        return;
    char text[32];
    std::snprintf(text, sizeof text, " | hp %08X", g_hash);
    line += text;
}

const FingerprintPart kParserFingerprint{"html parser", 5, &ParserFingerprint};
const DayHook kParserDrive{"html parser", 5, &DriveParserDay};

} // namespace
} // namespace opensc4::autotest
