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
//
// The layout test (OPENSC4_TEST_HTML_LAYOUT, fingerprint `hl`) is further
// down: the same pages and some of its own, laid out (html_layout.cpp).
#include "core/autotest_hooks.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <windows.h>

#include <algorithm>
#include <cctype>
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
    Shared, // a pointer to an object others share (a text node's font): its
            // vtable, and the pointer itself in the run's identity hash
    Vector, // begin, end, storage: the number of bytes
    TextPtr, // a pointer to a game string of its own (or null): its text
};

struct FieldAt {
    uint16_t offset;
    Field kind;
    uint16_t flag = 0; // when set: hashed only when the byte at this offset is set
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
        {0x00ACD7CC, true, {{0x28, Field::TextPtr}}},                                                // MAP
        {0x00ACD7F8, true, {}},                                                                   // OPTION
        {0x00ACD824, true, {}},                                                                   // P
        {0x00ACD850, true, {}},                                                                   // SELECT
        {0x00ACD87C, true, {}},                                                                   // SPAN
        {0x00ACD8A8, true, {{0x28, Field::Vector}, {0x34, Field::Word, 0x38}, {0x38, Field::Byte}}}, // TR
        {0x00ACD8D4, true, {}},                                                                   // TH
        {0x00ACE20C, true, {{0x28, Field::Word}}},                                                // UL, OL
        {0x00ACE238, true, {{0x28, Field::Word}, {0x2C, Field::Word}, {0x34, Field::Word}, {0x3C, Field::Word},
                            {0x44, Field::Word, 0x48}, {0x48, Field::Byte}, {0x4C, Field::Vector}, {0x58, Field::Vector},
                            {0x64, Field::Vector}, {0x70, Field::Word}, {0x74, Field::Word}, {0x78, Field::Word},
                            {0x7C, Field::Vector}, {0x88, Field::Vector}}},                       // TABLE
        {0x00ACE264, true, {{0x28, Field::Word}, {0x2C, Field::Word}, {0x34, Field::Word, 0x38}, {0x38, Field::Byte}, {0x39, Field::Byte}}}, // TD
        {0x00ACE290, true, {{0x28, Field::TextPtr}, {0x2C, Field::TextPtr}, {0x30, Field::TextPtr}}}, // A
        {0x00ACE2C0, true, {{0x28, Field::Word}, {0x2C, Field::Word}, {0x30, Field::Word}, {0x34, Field::Word},
                            {0x38, Field::Word}, {0x3C, Field::Word}, {0x40, Field::Byte}}},      // the body
        {0x00ACE300, false, {{0x1C, Field::Word}, {0x24, Field::Body}, {0x34, Field::Shared}, {0x38, Field::Word},
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

// The pointers to shared objects a parse hashed: the same in the game's
// parse and ours of one page within a run (the font system reuses its
// fonts), not from run to run, so compared but kept out of the fingerprint.
Fnv g_identity;

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
    if (field.flag && !At<uint8_t>(object, field.flag))
        return;
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
    case Field::Shared:
        g_identity.Word(At<uint32_t>(object, field.offset));
        [[fallthrough]];
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
    // Quotes never closed, an empty name, `=` with nothing after it.
    "<HTML><BODY><A HREF=\"never closed>x</A><A NAME='single>y</A><FONT =red COLOR=>z</FONT><A HREF=>w</A>"
    "<A HREF=\"a\"NAME=\"b\">v</A></BODY></HTML>",
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
    // Every phrase tag, each closed; font tags one by one; FONT's sizes.
    "<HTML><BODY><EM>em</EM><STRONG>strong</STRONG><DFN>dfn</DFN><CODE>code</CODE><SAMP>samp</SAMP><KBD>kbd</KBD>"
    "<VAR>var</VAR><CITE>cite</CITE><ABBR>abbr</ABBR><ACRONYM>acronym</ACRONYM><EM>open <B>bold</B> on</EM>"
    "<CODE>not closed</BODY></HTML>",
    "<HTML><BODY><TT>tt</TT><I>i</I><B>b</B><STRIKE>s</STRIKE><BIG>big</BIG><SMALL>small</SMALL><U>u</U>"
    "<FONT SIZE=7>7</FONT><FONT SIZE=0>0</FONT><FONT SIZE=+9>+9</FONT><FONT SIZE=-9>-9</FONT><FONT SIZE=+0>+0</FONT>"
    "<FONT SIZE=abc>abc</FONT><FONT SIZE=>none</FONT><FONT FACE=\"Courier New\" COLOR=blue>face</FONT>"
    "<FONT COLOR=\"#zz\">bad colour</FONT><B><P>block in B</P></B></BODY></HTML>",
    // Table parts in the order the game looks for them; COL and COLGROUP
    // (COLGROUP's end tag is given back and ends the table's parts).
    "<HTML><BODY><TABLE><COL><COL WIDTH=3><COLGROUP><COL><COL></COLGROUP><THEAD><TR><TH>h</TH></TR></THEAD>"
    "<TFOOT><TR><TD>foot</TD></TR></TFOOT><TBODY><TR><TD>body</TD></TR></TBODY><TBODY><TR><TD>second</TD>"
    "</TR></TBODY><TR><TD>bare row</TD></TR></TABLE></BODY></HTML>",
    "<HTML><BODY><TABLE><COLGROUP><TR><TD>after colgroup</TD></TR></TABLE><TABLE><CAPTION>c</CAPTION><TR></TR>"
    "</TABLE><TABLE><TR><TD VALIGN=top>t</TD><TD VALIGN=middle>m</TD><TD VALIGN=baseline>b</TD><TD VALIGN=other ALIGN=right>o</TD>"
    "<TD ALIGN=center>c</TD></TR><TR BGCOLOR=\"#FF00FF\"><TH>t</TH><TD>d</TD></TR></TABLE><TABLE BGCOLOR=red>"
    "<TR><TD>coloured</TD></TR></TABLE><TABLE></BODY></HTML>",
    "<HTML><BODY><TABLE><THEAD><TR><TH>h</TH></TR></THEAD><TFOOT><TR><TD>foot</TD></TR></TFOOT><TBODY><TR>"
    "<TD>body</TD></TR></TBODY></TABLE><TABLE><TFOOT><TR><TD>f</TD></TR><TR><TD>g</TD></TR></TABLE></BODY></HTML>",
    // Handlers that fail: empty lists, an unclosed list, a SELECT without
    // options, a MAP with nothing, DL without its end.
    "<HTML><BODY><UL></UL><OL></OL><UL><LI>never closed</BODY></HTML>",
    "<HTML><BODY><SELECT></SELECT><SELECT><OPTGROUP><OPTION>x</OPTGROUP><OPTION></SELECT><MAP NAME=e></MAP>"
    "<MAP><P>p in map</P></MAP><DL><DT>t<DD>d</DL><DL><DT>no end</BODY></HTML>",
    "<HTML><BODY><DIV ALIGN=center><DIV ALIGN=right><P>nested</P></DIV></DIV><CENTER><TABLE><TR><TD>in centre"
    "</TD></TR></TABLE></CENTER><BLOCKQUOTE><SCRIPT>x</SCRIPT><UL><LI>in quote</UL></BLOCKQUOTE><FORM><FORM>"
    "inner</FORM><SCRIPT></SCRIPT><P>f</P></FORM><H1>never closed</BODY></HTML>",
    // The head's tags.
    "<HTML><HEAD><TITLE>t</TITLE><BASE HREF=\"http://www.example.com/dir/\"><STYLE>B { x }</STYLE>"
    "<SCRIPT>s</SCRIPT><OBJECT>o</OBJECT><ISINDEX><META HTTP-EQUIV=refresh CONTENT=5></HEAD><BODY>"
    "<A HREF=rel.html>relative</A><IMG SRC=pic.png></BODY></HTML>",
    "<HTML><HEAD><TITLE>no end<P>after</HEAD><BODY>b</BODY></HTML>",
    // Text with entities in many runs, preformatted text, BR in odd places.
    "<HTML><BODY>&lt;tag&gt; &amp;amp; &nbsp;&nbsp;x&copy;y&#169;&#xA9;&#1234;&#x;&unknown<PRE>a\n\n  b\tc</PRE>"
    "<BR><B><BR>b</B></BODY></HTML>",
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
    for (const char* id : {"ui.html.parser.tokens", "ui.html.parser.names", "ui.html.parser.document",
                           "ui.html.parser.blocks", "ui.html.parser.table", "ui.html.parser.inline",
                           "ui.html.nodes.ctors", "ui.html.nodes.ctors.text", "ui.html.nodes.dtors", "ui.html.colors",
                           "ui.html.chars"})
        if (registry::ModeOf(id) == registry::Mode::Ours)
            return true;
    return false;
}

// Ours, then the ticker's rows, each one once; `own` the number of ours.
// OPENSC4_TEST_HTML_PARSER_FILE=<path>: the pages from a file instead of
// ours, separated by lines of `====` (for looking into one page).
std::vector<std::string> AllPages(std::vector<std::string> pages, std::size_t& own)
{
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
    own = pages.size();
    std::set<std::string> seen;
    for (std::string& row : TickerRows())
        if (seen.insert(row).second)
            pages.push_back(std::move(row));
    return pages;
}

// The HTML window of the tests: made by the class factory, never created on
// screen; its +0xD8 interface, with a reference.
void* MakeWindow(const char* test)
{
    void* const face = reinterpret_cast<void*(__cdecl*)()>(addr::HTMLWindow_Create)();
    if (!face) {
        log::Write("autotest: %s: no window", test);
        return nullptr;
    }
    Virtual<uint32_t>(face, 0x4);
    return face;
}

void DriveParserDay(const Day& day)
{
    if (!OnRunDay(day, "OPENSC4_TEST_HTML_PARSER") || !game::IsBuild641())
        return;
    SetUpService();
    void* const face = MakeWindow("html parser");
    if (!face)
        return;
    char* const window = static_cast<char*>(face) - 0xD8;

    std::size_t own = 0;
    const std::vector<std::string> pages = AllPages({std::begin(kPages), std::end(kPages)}, own);

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
        uint32_t gameIdentity = 0;
        if (compare) {
            html_parser::g_useGame = true;
            g_identity = Fnv{};
            game = run();
            gameIdentity = g_identity.h;
            html_parser::g_useGame = false;
        }
        g_identity = Fnv{};
        const uint32_t ours = run();
        total.Word(ours);
        if (compare && (game != ours || gameIdentity != g_identity.h)) {
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

// ====================================================================================
// The layout test

// OPENSC4_TEST_HTML_LAYOUT=<day> (fingerprint `hl`): the parser test's pages
// and some of the layout's own, each parsed with the document's default
// font steps and with the text page dialog's (SC4UI::TextPageFontSizes, as
// the window's slot 33 hands them to the document's slot 7), and laid out
// at 300 and 450 pixels as the window lays a page out: a layout context
// (0x68 bytes) with the client rectangle and the view (the second width
// scrolled across a little, which the start of a layout treats on its own),
// the body's Layout (slot 3) and second pass (slot 4). Hashed: every node's
// flags and box and the fields the layout sets (a text run's progress and
// its pieces, an image's and a rule's sizes, a cell's), the context after
// (line, pen, margins, the items left, the left and right entries), and
// for each box the context's view test and its window coordinates.
//
// With any layout or parser group ours, each page is laid out twice, as the
// parser test does: through the game's functions and through ours.

const char* const kLayoutPages[] = {
    // Long text that wraps, a word longer than either width, runs of spaces.
    "<HTML><BODY>The quick brown fox jumps over the lazy dog again and again, until the line has to break "
    "somewhere and then once more, and the dog is still lazy.   Spaces    between     words.     "
    "Supercalifragilisticexpialidociousandthensomemorelettersthatnevermeetaspaceatall end.</BODY></HTML>",
    // Alignment: centred and right paragraphs and blocks, mixed in one line.
    "<HTML><BODY><P ALIGN=center>centred text that runs over more than one line of the narrow width, centred "
    "each time</P><P ALIGN=right>right aligned text, also long enough to wrap at three hundred pixels wide</P>"
    "<CENTER>c <B>bold</B> c</CENTER><DIV ALIGN=right>r<BR>r2</DIV>left <DIV ALIGN=center>mid</DIV> after"
    "</BODY></HTML>",
    // Floating images, left and right, with text around and after them.
    "<HTML><BODY><IMG SRC=a.png WIDTH=60 HEIGHT=40 ALIGN=left>Text beside a left float that goes on long "
    "enough to pass below it at either width, and then some more words to fill the lines.<IMG SRC=b.png "
    "WIDTH=80 HEIGHT=30 ALIGN=right>More text beside a right float, long enough to wrap around it as well."
    "<BR><IMG SRC=c.png WIDTH=500 HEIGHT=10 ALIGN=right>wide<IMG SRC=d.png WIDTH=20 HEIGHT=200 ALIGN=left>"
    "tall<P>after the floats</P></BODY></HTML>",
    // Images in the line, every ALIGN, too wide for the line, no size.
    "<HTML><BODY>a<IMG SRC=a.png WIDTH=30 HEIGHT=30>b<IMG SRC=b.png WIDTH=30 HEIGHT=30 ALIGN=top>c"
    "<IMG SRC=c.png WIDTH=30 HEIGHT=31 ALIGN=middle>d<IMG SRC=d.png WIDTH=30 HEIGHT=30 ALIGN=bottom>e"
    "<IMG SRC=e.png WIDTH=30 HEIGHT=30 ALIGN=absmiddle>f<IMG SRC=f.png WIDTH=400 HEIGHT=20>g"
    "<IMG SRC=g.png WIDTH=300 HEIGHT=20>h<IMG SRC=h.png><IMG SRC=i.png WIDTH=0 HEIGHT=5><IMG SRC=j.png "
    "WIDTH=5></BODY></HTML>",
    // Rules: widths, percentages over 100, sizes 0 and negative.
    "<HTML><BODY>x<HR>y<HR WIDTH=50%><HR WIDTH=150%><HR WIDTH=0%><HR WIDTH=-5><HR WIDTH=1000><HR SIZE=0>"
    "<HR SIZE=-3 WIDTH=20><HR SIZE=12 WIDTH=75%>z</BODY></HTML>",
    // Lists in lists, numbered lists long enough to wrap, a quote in a list.
    "<HTML><BODY><OL><LI>first item with enough text to wrap onto a second line at the narrow width "
    "<LI>second<OL><LI>inner one<LI>inner two with more words than fit on one line of the narrow page</OL>"
    "<LI>third<UL><LI>bullet<UL><LI>deeper<BLOCKQUOTE>quoted in a list, a long quote that wraps</BLOCKQUOTE>"
    "</UL></UL></OL><DL><DT>term<DD>a definition long enough to wrap onto the next line of the page</DL>"
    "</BODY></HTML>",
    // Preformatted text, NOBR, breaks in a row, empty paragraphs.
    "<HTML><BODY><PRE>line one is long and is not broken however narrow the page is, it just runs on\n"
    "line two\n\n   indented four\n</PRE><NOBR>no break here even though this is long enough to wrap</NOBR>"
    "<BR><BR><BR>three breaks<P></P><P></P>two empty paragraphs<BR>end</BODY></HTML>",
    // Headings and fonts of every size in one paragraph (a line of mixed
    // heights), quotes in quotes.
    "<HTML><BODY><H1>Heading one wraps at the narrow width</H1><H3>three</H3><H6>six</H6><P>small "
    "<FONT SIZE=1>one</FONT> <FONT SIZE=7>seven</FONT> <BIG>big</BIG> <SMALL>small</SMALL> <TT>typewriter "
    "text</TT> and on</P><BLOCKQUOTE>outer<BLOCKQUOTE>inner quote that runs over the line's end at three "
    "hundred</BLOCKQUOTE>outer again</BLOCKQUOTE></BODY></HTML>",
    // Tables: widths, cells with lots of text, NOWRAP, nested, floats in a
    // cell, empty cells.
    "<HTML><BODY><TABLE BORDER=1 WIDTH=100% CELLPADDING=2><TR><TD WIDTH=50>narrow cell with a lot of text "
    "in it</TD><TD>wide cell<BR>two lines</TD><TD NOWRAP>no wrap in this cell at all</TD></TR><TR><TD></TD>"
    "<TD><IMG SRC=a.png WIDTH=20 HEIGHT=20 ALIGN=left>float in a cell with text</TD><TD><TABLE><TR><TD>in"
    "</TD><TD>ner</TD></TR></TABLE></TD></TR></TABLE><TABLE WIDTH=120><TR><TD>fixed table width</TD></TR>"
    "</TABLE>after</BODY></HTML>",
    // A link and spans in a wrapped line, an image in a link.
    "<HTML><BODY>Before <A HREF=x.html>a link whose text is long enough to wrap at the narrow width</A> "
    "and <SPAN STYLE=\"color: #FF0000\">a span</SPAN>, <A HREF=y.html><IMG SRC=a.png WIDTH=16 HEIGHT=16>"
    "</A> after.</BODY></HTML>",
    // Floats ending on a line's top or just before or after it (lines of
    // the default font are 16 high), on both sides.
    "<HTML><BODY><IMG SRC=a.png WIDTH=40 HEIGHT=16 ALIGN=right>beside<BR>the words of a line long enough "
    "to run up to the right margin of the page and on<BR><IMG SRC=b.png WIDTH=40 HEIGHT=15 ALIGN=right>be"
    "side<BR>the words of a line long enough to run up to the right margin of the page and on<BR><IMG SRC"
    "=c.png WIDTH=40 HEIGHT=17 ALIGN=right>beside<BR>the words of a line long enough to run up to the rig"
    "ht margin of the page and on<BR><IMG SRC=d.png WIDTH=40 HEIGHT=32 ALIGN=right>beside<BR>the words of"
    " a line long enough to run up to the right margin of the page and on<BR><IMG SRC=e.png WIDTH=40 HEIG"
    "HT=16 ALIGN=left>beside<BR>the words of a line long enough to run up to the right margin of the page"
    " and on<BR><IMG SRC=f.png WIDTH=40 HEIGHT=32 ALIGN=left>beside<BR>the words of a line long enough to"
    " run up to the right margin of the page and on<BR></BODY></HTML>",
    // Words longer than the line, centred and right aligned (the width left
    // after a forced word aligns the line). Not in a list: there the game
    // may not force it (html_layout.cpp, IndentOf), and with no group ours a
    // run did not get through a page of words too long for their list.
    "<HTML><BODY><P ALIGN=center>Pneumonoultramicroscopicsilicovolcanoconiosisandthensomemoreletters "
    "short</P><P ALIGN=right>Pneumonoultramicroscopicsilicovolcanoconiosisandthensomemoreletters short</P>"
    "<DIV ALIGN=center>Antidisestablishmentarianismistic</DIV></BODY></HTML>",
    // Tables (0x0090A448): columns sharing the width left by how much wider
    // each could be, a short and a long column, more columns than fit
    // (their narrowest past the width), a table after text in a line.
    "<HTML><BODY><TABLE><TR><TD>short</TD><TD>a much longer cell whose words go on and on past the width of "
    "the page at either size</TD></TR><TR><TD>second row, also with some words</TD><TD>x</TD></TR></TABLE>"
    "words before <TABLE><TR><TD>one</TD><TD>two</TD><TD>three</TD></TR></TABLE>after<TABLE><TR><TD>"
    "Incomprehensibilities</TD><TD>Counterrevolutionaries</TD><TD>Uncharacteristically</TD><TD>"
    "Electroencephalograph</TD><TD>Interdisciplinary</TD></TR></TABLE>end</BODY></HTML>",
    // Nested tables, three deep, beside cells of text; a word longer than
    // its column (the widths shared again).
    "<HTML><BODY><TABLE><TR><TD>outer text that wraps a little</TD><TD><TABLE><TR><TD>mid</TD><TD><TABLE>"
    "<TR><TD>inner one</TD><TD>inner two words</TD></TR><TR><TD>i3</TD></TR></TABLE></TD></TR><TR><TD>"
    "Supercalifragilisticexpialidocious</TD><TD>m</TD></TR></TABLE></TD><TD>last</TD></TR></TABLE>"
    "<TABLE><TR><TD>a b c d e f g h i j k l m n o p q r s t u v w x y z</TD><TD>Pseudopseudohypoparathyroidism"
    "</TD></TR></TABLE></BODY></HTML>",
    // Cell widths in pixels (narrower and wider than their words, wider
    // than the page) and in percent; BORDER, CELLPADDING, CELLSPACING and
    // the table's WIDTH (none read by the layout).
    "<HTML><BODY><TABLE BORDER=2 CELLPADDING=5 CELLSPACING=3 WIDTH=80%><TR><TD WIDTH=40>forty pixels of "
    "words</TD><TD WIDTH=120>one hundred and twenty</TD><TD WIDTH=50%>half</TD></TR><TR><TD WIDTH=10>ten"
    "</TD><TD WIDTH=600>wider than the page</TD><TD>plain</TD></TR></TABLE><TABLE WIDTH=200 BORDER=0>"
    "<TR><TD WIDTH=30>Thirtypixelsisnotenoughforthis</TD><TD WIDTH=0>zero</TD><TD WIDTH=-4>minus</TD>"
    "</TR></TABLE></BODY></HTML>",
    // Rows of 1, 3 and 2 cells, empty cells, TH mixed with TD (TH is not a
    // cell of the row's), COLSPAN and ROWSPAN (not read), a row of TH only,
    // a table of no rows, a table of a row of TH only (no columns).
    "<HTML><BODY><TABLE><TR><TD>one</TD></TR><TR><TD>a</TD><TD></TD><TD>c c c</TD></TR><TR><TD></TD>"
    "<TD>2</TD></TR><TR><TH>head</TH><TD>d</TD><TH>h2</TH><TD>e</TD></TR><TR><TD COLSPAN=3>spans three"
    "</TD></TR><TR><TD ROWSPAN=2>two rows</TD><TD>r</TD></TR><TR><TH>only a head</TH></TR></TABLE>x"
    "<TABLE></TABLE>y<TABLE><TR><TH>no cells</TH></TR></TABLE>z</BODY></HTML>",
    // Tables beside a left float and a right one, in a centred block, with
    // images and breaks in cells, VALIGN and NOWRAP, text after on the line.
    // No list in a cell: with the groups off, a run did not get through
    // this page with a UL in its last cell (an indent's bottom again,
    // html_layout.cpp, IndentOf).
    "<HTML><BODY><IMG SRC=a.png WIDTH=50 HEIGHT=60 ALIGN=left><IMG SRC=b.png WIDTH=40 HEIGHT=90 ALIGN=right>"
    "<TABLE><TR><TD VALIGN=top>top<BR>two<BR>three</TD><TD VALIGN=bottom><IMG SRC=c.png WIDTH=30 HEIGHT=30>"
    "</TD><TD NOWRAP>no wrap here</TD></TR></TABLE>beside<CENTER><TABLE><TR><TD>centred table cell</TD><TD>"
    "<IMG SRC=d.png WIDTH=70 HEIGHT=10 ALIGN=right>float</TD></TR></TABLE></CENTER><TABLE><TR><TD><P>para "
    "in a cell</P><HR WIDTH=50%><H2>head</H2></TD><TD>listed</TD></TR></TABLE>after</BODY></HTML>",
    // Fonts of different sizes across a row, a table in a quote (short
    // words: an indent's bottom stays out of it), many short columns.
    "<HTML><BODY><TABLE><TR><TD><FONT SIZE=7>big</FONT></TD><TD><FONT SIZE=1>small text in a small font "
    "that wraps</FONT></TD></TR></TABLE><BLOCKQUOTE><TABLE><TR><TD>in a</TD><TD>quote</TD></TR></TABLE>"
    "</BLOCKQUOTE><TABLE><TR><TD>1</TD><TD>2</TD><TD>3</TD><TD>4</TD><TD>5</TD><TD>6</TD><TD>7</TD><TD>8</TD>"
    "<TD>9</TD><TD>10</TD><TD>11</TD><TD>12</TD></TR><TR><TD>a b</TD><TD>c d e</TD><TD>f</TD></TR></TABLE>"
    "</BODY></HTML>",
};

// The text page dialog's font steps (SC4UI::TextPageFontSizes).
const int32_t* TextPageSteps() { return reinterpret_cast<const int32_t*>(addr::SC4UI_TextPageFontSizes); }

// The layout's own hash of a node: its flags and box and what the layout
// set, its children's after.
void HashLayoutNode(Fnv& f, char* ctx, char* node, int depth)
{
    f.Byte(0x5A);
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
    f.Byte(At<uint8_t>(node, 0x8));
    f.Byte(At<uint8_t>(node, 0x9));
    Trace(" b%X", At<uint8_t>(node, 0x8));
    Trace(" b%X |", At<uint8_t>(node, 0x9));
    for (std::size_t at = 0xC; at < 0x1C; at += 4) {
        f.Word(At<uint32_t>(node, at));
        Trace(" %d", At<uint32_t>(node, at));
    }
    // The view test and the window's coordinates of the box.
    using InViewFn = uint8_t(__thiscall*)(void*, const void*, uint8_t);
    using RectFn = void*(__thiscall*)(void*, const void*, void*);
    using PointFn = int32_t(__thiscall*)(void*, int32_t, int32_t, int32_t*, int32_t*);
    const uint8_t part = reinterpret_cast<InViewFn>(addr::HTMLLayout_InView)(ctx, node + 0xC, 0);
    const uint8_t whole = reinterpret_cast<InViewFn>(addr::HTMLLayout_InView)(ctx, node + 0xC, 1);
    int32_t window[4] = {};
    reinterpret_cast<RectFn>(addr::HTMLLayout_ToWindowRect)(ctx, node + 0xC, window);
    int32_t point[2] = {};
    reinterpret_cast<PointFn>(addr::HTMLLayout_ToWindowPoint)(ctx, At<int32_t>(node, 0xC), At<int32_t>(node, 0x10),
                                                              &point[0], &point[1]);
    f.Byte(part);
    f.Byte(whole);
    for (const int32_t v : window)
        f.Word(static_cast<uint32_t>(v));
    f.Word(static_cast<uint32_t>(point[0]));
    f.Word(static_cast<uint32_t>(point[1]));
    Trace(" v%X", part | whole << 1);
    Trace(" w%d", window[0]);
    Trace(",%d", window[1]);
    Trace(",%d", window[2]);
    Trace(",%d", window[3]);
    Trace(" p%d", point[0]);
    Trace(",%d", point[1]);
    switch (vtable) {
    case 0x00ACE300: { // text: progress, the fit, the pieces
        f.Word(At<uint32_t>(node, 0x44));
        f.Word(At<uint32_t>(node, 0x48));
        Trace(" @%X", At<uint32_t>(node, 0x44));
        Trace("+%X", At<uint32_t>(node, 0x48));
        const uint32_t* first = At<const uint32_t*>(node, 0x4C);
        const uint32_t* last = At<const uint32_t*>(node, 0x50);
        const uint32_t count = first && first <= last ? static_cast<uint32_t>(last - first) : 0;
        f.Word(count);
        for (uint32_t i = 0; i < count && i < 60000; ++i) {
            f.Word(first[i]);
            Trace(i % 6 == 0 ? " [%d" : (i % 6 == 5 ? " %d]" : " %d"), first[i]);
        }
        break;
    }
    case 0x00ACE188: // IMG
        for (std::size_t at = 0x2C; at <= 0x38; at += 4)
            f.Word(At<uint32_t>(node, at));
        break;
    case 0x00ACD55C: // HR
        f.Word(At<uint32_t>(node, 0x20));
        f.Word(At<uint32_t>(node, 0x24));
        break;
    case 0x00ACE264: // TD (+0x30 is read only when +0x2C is 0, and the
                     // game leaves it as the heap had it otherwise)
        f.Word(At<uint32_t>(node, 0x2C));
        if (At<uint32_t>(node, 0x2C) == 0)
            f.Word(At<uint32_t>(node, 0x30));
        Trace(" td %X", At<uint32_t>(node, 0x2C));
        Trace(" %X", At<uint32_t>(node, 0x30));
        break;
    case 0x00ACE238: { // TABLE: its columns (7 words each), the rows'
                       // heights and tops
        for (const std::size_t vector : {std::size_t{0x70}, std::size_t{0x7C}, std::size_t{0x88}}) {
            const uint32_t* first = At<const uint32_t*>(node, vector);
            const uint32_t* last = At<const uint32_t*>(node, vector + 4);
            const uint32_t count = first && first <= last ? static_cast<uint32_t>(last - first) : 0;
            f.Word(count);
            Trace(vector == 0x70 ? " cols" : (vector == 0x7C ? " heights" : " tops"), 0);
            for (uint32_t i = 0; i < count && i < 60000; ++i) {
                f.Word(first[i]);
                Trace(" %d", first[i]);
            }
        }
        break;
    }
    default:
        break;
    }
    const NodeClass* const c = ClassOf(vtable);
    if (c && c->container) {
        char** const first = At<char**>(node, 0x1C);
        char** const last = At<char**>(node, 0x20);
        const uint32_t count = first && first <= last ? static_cast<uint32_t>(last - first) : 0;
        f.Word(count);
        for (uint32_t i = 0; i < count && i < 100000; ++i)
            HashLayoutNode(f, ctx, first[i], depth + 1);
    } else if (!c) {
        ++g_unknown;
    }
}

// The context after a layout: the line, pen and margins, the items left, the
// left entries (their padding and an indent's bottom are what the stack had)
// and the right ones.
void HashContext(Fnv& f, char* ctx)
{
    Trace("\nctx", 0);
    for (std::size_t at = 0x2C; at <= 0x48; at += 4) {
        f.Word(At<uint32_t>(ctx, at));
        Trace(" %d", At<uint32_t>(ctx, at));
    }
    char* const header = At<char*>(ctx, 0x4C);
    uint32_t items = 0;
    for (char* node = At<char*>(header, 0); node != header && items < 100000; node = At<char*>(node, 0))
        ++items;
    f.Word(items);
    Trace(" items %d", items);
    for (char* e = At<char*>(ctx, 0x50); e && e < At<char*>(ctx, 0x54); e += 16) {
        f.Byte(At<uint8_t>(e, 0));
        if (At<uint8_t>(e, 0))
            f.Word(At<uint32_t>(e, 4));
        f.Word(At<uint32_t>(e, 8));
        f.Word(At<uint32_t>(e, 12));
        Trace(" L%d", At<uint8_t>(e, 0));
    }
    for (char* e = At<char*>(ctx, 0x5C); e && e < At<char*>(ctx, 0x60); e += 8) {
        f.Word(At<uint32_t>(e, 0));
        f.Word(At<uint32_t>(e, 4));
        Trace(" R%d", At<uint32_t>(e, 4));
    }
}

void PaintPage(Fnv& f, char* root, char* ctx, int variant);

// One page parsed (with the font steps given, or the default ones) and laid
// out `width` wide; the hash of the boxes. With `paint`, the page is then
// painted, hit-tested and its links pressed (the paint test, below), that
// hash in `paint`.
uint32_t LayoutPage(char* window, const std::string& page, const int32_t* steps, int32_t width, bool scrolled,
                    Fnv* paint = nullptr, int variant = 0)
{
    using NewFn = void*(__cdecl*)(std::size_t);
    using ThisFn = void*(__thiscall*)(void*);
    using InitFn = uint8_t(__thiscall*)(void*, void*, void*, const GameText*);
    char* const doc = static_cast<char*>(reinterpret_cast<NewFn>(addr::SC4_operator_new)(0x1C4));
    if (!doc)
        return 0;
    reinterpret_cast<ThisFn>(addr::HTMLDocument_Construct)(doc);
    Virtual<uint32_t>(doc, 0x4);
    GameText base;
    MakeText(&base, "");
    reinterpret_cast<InitFn>(addr::HTMLDocument_Init)(doc, window, &g_service, &base);
    DestroyText(&base);
    if (steps)
        Virtual<void>(doc, 0x1C, steps, 7); // as ShowHTML hands on the window's steps
    GameText text;
    MakeText(&text, page.c_str());
    const bool parsed = Virtual<uint8_t>(doc, 0x20, &text) != 0;
    DestroyText(&text);
    const bool dump = EnvInt("OPENSC4_TEST_HTML_PARSER_DUMP", 0) != 0;
    if (dump)
        log::Write("autotest: html layout: parsed, laying out");
    Fnv f;
    f.Byte(parsed ? 1 : 0);
    char* const root = At<char*>(doc, 0x14);
    if (root) {
        alignas(8) char ctx[0x68] = {};
        reinterpret_cast<ThisFn>(addr::HTMLLayout_Construct)(ctx);
        // The client rectangle (+0xC) and the view (+0x1C), as the window
        // sets them; the scrolled one 7 across and 0 down.
        const int32_t scroll = scrolled ? 7 : 0;
        const int32_t rects[8] = {2, 3, width + 2, 403, scroll, 0, width + scroll, 400};
        std::memcpy(ctx + 0xC, rects, sizeof rects);
        Virtual<void>(root, 0xC, ctx); // Layout
        Virtual<void>(root, 0x10);     // the second pass
        if (dump)
            log::Write("autotest: html layout: laid out, hashing");
        HashLayoutNode(f, ctx, root, 0);
        HashContext(f, ctx);
        if (paint)
            PaintPage(*paint, root, ctx, variant);
        reinterpret_cast<void(__thiscall*)(void*)>(addr::HTMLLayout_Destruct)(ctx);
    }
    Virtual<uint32_t>(doc, 0x8);
    return f.h;
}

bool g_layoutOn = false;
uint32_t g_layoutHash = 2166136261u;

bool AnyLayoutOurs()
{
    for (const char* id : {"ui.html.layout", "ui.html.nodes.layout.blocks", "ui.html.nodes.layout.text",
                           "ui.html.nodes.layout.images", "ui.html.nodes.layout.cells",
                           "ui.html.nodes.layout.tables"})
        if (registry::ModeOf(id) == registry::Mode::Ours)
            return true;
    return AnyOurs();
}

// The parser test's pages, the layout's own and the sweep of fixed widths,
// then the ticker's rows; `own` the number before the rows.
std::vector<std::string> LayoutTestPages(std::size_t& own)
{
    std::vector<std::string> mine(std::begin(kPages), std::end(kPages));
    mine.insert(mine.end(), std::begin(kLayoutPages), std::end(kLayoutPages));
    // Cells of every fixed width from 20 to 80 pixels with the same two
    // words: at one of them the words just fit (a fixed width's margins are
    // seen only to the pixel).
    std::string sweep = "<HTML><BODY>";
    for (int width = 20; width <= 80; ++width)
        sweep += "<TABLE><TR><TD WIDTH=" + std::to_string(width) + ">ab ab</TD><TD>x</TD></TR></TABLE>";
    mine.push_back(sweep + "</BODY></HTML>");
    return AllPages(std::move(mine), own);
}

void DriveLayoutDay(const Day& day)
{
    if (!OnRunDay(day, "OPENSC4_TEST_HTML_LAYOUT") || !game::IsBuild641())
        return;
    SetUpService();
    void* const face = MakeWindow("html layout");
    if (!face)
        return;
    char* const window = static_cast<char*>(face) - 0xD8;

    std::size_t own = 0;
    const std::vector<std::string> pages = LayoutTestPages(own);

    const bool compare = AnyLayoutOurs();
    Fnv total;
    int differ = 0;
    int nodes = 0;
    struct Variant {
        const char* name;
        const int32_t* steps;
        int32_t width;
        bool scrolled;
    };
    const Variant variants[] = {
        {"d300", nullptr, 300, true},
        {"d450", nullptr, 450, false},
        {"t300", TextPageSteps(), 300, true},
        {"t450", TextPageSteps(), 450, false},
    };
    for (std::size_t i = 0; i < pages.size(); ++i) {
        uint32_t hashes[4] = {};
        for (int v = 0; v < 4; ++v) {
            const Variant& variant = variants[v];
            if (EnvInt("OPENSC4_TEST_HTML_PARSER_DUMP", 0))
                log::Write("autotest: html layout: page %zu %s", i, variant.name);
            auto run = [&] { return LayoutPage(window, pages[i], variant.steps, variant.width, variant.scrolled); };
            uint32_t game = 0;
            if (compare) {
                html_parser::g_useGame = true;
                game = run();
                html_parser::g_useGame = false;
            }
            g_nodes = 0;
            const uint32_t ours = run();
            nodes += g_nodes;
            hashes[v] = ours;
            total.Word(ours);
            if (compare && game != ours) {
                ++differ;
                log::Write("autotest: html layout: page %zu %s differs: game %08X ours %08X (%zu bytes)", i,
                           variant.name, game, ours, pages[i].size());
                if (differ <= 8) {
                    std::string trace[2];
                    for (int side = 0; side < 2; ++side) {
                        html_parser::g_useGame = side == 0;
                        g_trace = &trace[side];
                        run();
                        g_trace = nullptr;
                    }
                    html_parser::g_useGame = false;
                    log::Write("autotest: html layout: page %zu %s: %s\n  game:%s\n  ours:%s", i, variant.name,
                               pages[i].c_str(), trace[0].c_str(), trace[1].c_str());
                }
            }
        }
        if (i < own || EnvInt("OPENSC4_TEST_HTML_PARSER_DUMP", 0))
            log::Write("autotest: html layout: page %zu %08X %08X %08X %08X", i, hashes[0], hashes[1], hashes[2],
                       hashes[3]);
    }
    Virtual<uint32_t>(face, 0x8);

    g_layoutHash = total.h;
    g_layoutOn = true;
    log::Write("autotest: html layout: %zu pages (%zu ours, %zu ticker rows), 4 layouts each, %d nodes; %s; hash %08X",
               pages.size(), own, pages.size() - own, nodes,
               compare ? (differ ? "DIFFERENCES" : "game and ours the same") : "the game's only", g_layoutHash);
    if (compare)
        log::Write("autotest: html layout: %d differences in %zu layouts", differ, pages.size() * 4);
}

void LayoutFingerprint(cISC4City*, std::string& line)
{
    if (!g_layoutOn)
        return;
    char text[32];
    std::snprintf(text, sizeof text, " | hl %08X", g_layoutHash);
    line += text;
}

const FingerprintPart kLayoutFingerprint{"html layout", 5, &LayoutFingerprint};
const DayHook kLayoutDrive{"html layout", 5, &DriveLayoutDay};

// ====================================================================================
// The paint test

// OPENSC4_TEST_HTML_PAINT=<day> (fingerprint `hd`): the layout test's pages
// and layouts, each then painted (the body's Draw, slot 5) three times, the
// view as laid out, 150 pixels lower, and as laid out with the text runs and
// images marked not laid out (each lays itself out as it is drawn, on the
// context's line or a cell's copy of it; the boxes and pieces hashed after),
// hit-tested on a grid of points (the
// body's slot 7) and its links pressed and let go (slots 8 and 9 of each
// A). Nothing is drawn: the context's window (+0), document (+4) and
// picture (+8) are stand-ins whose every slot records the call (the slot,
// its arguments, the rectangles and the text they point to) into the hash
// and answers as the game's would; so are the pictures given to two of
// every three images (one the size of WIDTH and HEIGHT, one a pixel wider,
// for the blit and the stretch blit), the stretch blit's interfaces, and the
// fonts of the text runs (a stand-in that hands every slot to the font but
// DrawString, slot 51). The picture's begin answers false every fifth time.
// HR's colours (made once by the game, kept in its statics) are made again
// for every paint and put back after the test; a page with BLINK is painted
// at a time of the game's clock in the first half of a second or the second
// (by variant and pass), 300 ms clear of the turn.
//
// With any parser, layout, paint, hit or link group ours, each layout is run
// through the game's functions and through ours, the hashes of the layout
// and of the paint compared, a layout that differs logged with both records.

constexpr int kStandSlots = 128;
constexpr int kFontDraw = 51;

enum StandKind : uint32_t {
    kStandWindow = 1,
    kStandDocument,
    kStandPicture,
    kStandSource,
    kStandBlitter,
    kStandBlit,
    kStandFont,
};

// A stand-in: its vtable; the font it hands its slots to (+4, read by the
// font's thunks); for a picture given to an image, its size and area.
struct Stand {
    void** vtable;
    char* real;
    uint32_t kind;
    uint32_t id;
    int32_t width;
    int32_t height;
    int32_t area[4];
};

void** g_recordTable = nullptr; // every slot recorded
void** g_fontTable = nullptr;   // every slot the font's, but DrawString

Stand g_standWindow{};
Stand g_standDocument{};
Stand g_standPicture{};
Stand g_standBlitter{};
Stand g_standBlit{};
constexpr int kMaxSources = 64;
Stand g_sources[kMaxSources];
int g_sourceCount = 0;
constexpr int kMaxFonts = 64;
Stand g_fonts[kMaxFonts];
int g_fontCount = 0;

Fnv* g_paintHash = nullptr;   // the hash the stand-ins record into
char* g_bodyAnswer = nullptr; // the document's slot 12
uint32_t g_begins = 0;
uint32_t g_paintCalls = 0;
uint32_t g_kindCalls[8] = {};
uint32_t g_strayCalls = 0;
uint32_t g_probes = 0;

uint32_t& Static(std::uintptr_t address) { return *reinterpret_cast<uint32_t*>(address); }

// What a stand-in's slot takes: i a dword, p a pointer only null or not, o
// an object (a stand-in by its kind and number), r a rectangle (null or its
// four sides), s a text whose count is the next argument. Null for a slot
// no paint calls.
const char* SlotSpec(uint32_t kind, uint32_t slot)
{
    switch (kind) {
    case kStandWindow:
        return slot == 108 ? "iii" : nullptr; // a colour made
    case kStandDocument:
        return slot == 12 || slot == 17 || slot == 18 || slot == 19 ? "" : nullptr;
    case kStandPicture:
        switch (slot) {
        case 0:
            return "ip"; // QueryInterface
        case 6:
            return "i"; // begin
        case 7:
            return "i"; // end
        case 20:
            return "irr"; // fill
        case 28:
            return "iiiiii"; // line
        case 29:
            return "orrr"; // blit
        default:
            return nullptr;
        }
    case kStandSource:
        return slot == 9 || slot == 10 || slot == 12 ? "" : nullptr;
    case kStandBlitter:
        return slot == 2 || slot == 4 ? "" : (slot == 9 ? "o" : nullptr);
    case kStandBlit:
        return slot == 3 || slot == 9 || slot == 40 ? "" : (slot == 5 ? "r" : (slot == 38 ? "orr" : nullptr));
    case kStandFont:
        return slot == kFontDraw ? "oiisiiirp" : nullptr;
    default:
        return nullptr;
    }
}

const Stand* StandAt(uint32_t value)
{
    const Stand* const p = reinterpret_cast<const Stand*>(static_cast<uintptr_t>(value));
    for (const Stand* s : {&g_standWindow, &g_standDocument, &g_standPicture, &g_standBlitter, &g_standBlit})
        if (p == s)
            return s;
    if (p >= g_sources && p < g_sources + kMaxSources)
        return p;
    if (p >= g_fonts && p < g_fonts + kMaxFonts)
        return p;
    return nullptr;
}

void RecordArguments(Fnv& f, const char* spec, const uint32_t* args)
{
    for (std::size_t i = 0; spec[i]; ++i) {
        const uint32_t a = args[i];
        switch (spec[i]) {
        case 'i':
            f.Word(a);
            Trace(" %d", a);
            break;
        case 'p':
            f.Byte(a ? 1 : 0);
            Trace(a ? " p" : " 0", 0);
            break;
        case 'o': {
            const Stand* const s = StandAt(a);
            const uint32_t which = s ? s->kind << 16 | s->id : 0xFFFFFFFFu;
            f.Word(which);
            Trace(" o%X", which);
            break;
        }
        case 'r':
            f.Byte(a ? 1 : 0);
            if (a) {
                const int32_t* const r = reinterpret_cast<const int32_t*>(static_cast<uintptr_t>(a));
                for (int k = 0; k < 4; ++k) {
                    f.Word(static_cast<uint32_t>(r[k]));
                    Trace(k ? ",%d" : " [%d", static_cast<uint32_t>(r[k]));
                }
                Trace("]", 0);
            } else {
                Trace(" null", 0);
            }
            break;
        case 's': {
            const char* const text = reinterpret_cast<const char*>(static_cast<uintptr_t>(a));
            const uint32_t n = text && args[i + 1] < 100000 ? args[i + 1] : 0;
            f.Bytes(text, text + n);
            if (g_trace && text)
                *g_trace += " \"" + std::string(text, n) + "\"";
            break;
        }
        default:
            break;
        }
    }
}

// A stand-in's slot: recorded, and the answer (low dword) with the bytes of
// arguments to take off (high dword).
uint64_t __cdecl RecordCall(Stand* self, uint32_t slot, const uint32_t* args)
{
    const char* const spec = SlotSpec(self->kind, slot);
    if (!spec) {
        ++g_strayCalls;
        log::Write("autotest: html paint: stand-in %u slot %u not known", self->kind, slot);
        return 0;
    }
    ++g_paintCalls;
    ++g_kindCalls[self->kind & 7];
    if (Fnv* const f = g_paintHash) {
        f->Byte(0xC5);
        f->Word(self->kind << 16 | self->id);
        f->Word(slot);
        Trace("\n  %u", self->kind);
        Trace(".%u", self->id);
        Trace(".%u(", slot);
        RecordArguments(*f, spec, args);
        Trace(" )", 0);
    }
    uint32_t answer = 1;
    switch (self->kind) {
    case kStandWindow:
        answer = 0xFF000000u | (args[0] & 0xFF) << 16 | (args[1] & 0xFF) << 8 | (args[2] & 0xFF);
        break;
    case kStandDocument:
        answer = slot == 12 ? static_cast<uint32_t>(reinterpret_cast<uintptr_t>(g_bodyAnswer))
                            : (slot == 18 ? 0x00112233u : (slot == 19 ? 0x00445566u : 0));
        break;
    case kStandPicture:
        if (slot == 0) {
            answer = 0;
            if (args[0] == 0xAB300B2Bu && args[1]) {
                *reinterpret_cast<Stand**>(static_cast<uintptr_t>(args[1])) = &g_standBlitter;
                answer = 1;
            }
        } else if (slot == 6) {
            answer = ++g_begins % 5 == 0 ? 0 : 1;
        }
        break;
    case kStandSource:
        answer = slot == 9 ? static_cast<uint32_t>(self->width)
                           : (slot == 10 ? static_cast<uint32_t>(self->height)
                                         : static_cast<uint32_t>(reinterpret_cast<uintptr_t>(self->area)));
        break;
    case kStandBlitter:
        answer = slot == 4 ? static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&g_standBlit)) : (slot == 2 ? 0 : 1);
        break;
    default:
        break;
    }
    return static_cast<uint64_t>(std::strlen(spec) * 4) << 32 | answer;
}

// Every recording slot pushes its number and comes here: the recorder's
// answer in EAX, its arguments taken off as the slot's `ret` would.
__declspec(naked) void RecordThunk()
{
    __asm push ebp
    __asm mov ebp, esp
    __asm lea eax, [ebp + 0xC]
    __asm push eax
    __asm push dword ptr [ebp + 4]
    __asm push ecx
    __asm call RecordCall
    __asm add esp, 12
    __asm mov ecx, [ebp + 8]
    __asm mov esp, ebp
    __asm pop ebp
    __asm lea esp, [esp + edx + 8]
    __asm jmp ecx
}

// The two vtables, their slots made in a page of code: a recording slot is
// `push n; jmp RecordThunk`, a font's `mov ecx, [ecx+4]; mov eax, [ecx];
// jmp [eax+4n]`.
bool MakeStandTables()
{
    if (g_recordTable)
        return true;
    uint8_t* const code =
        static_cast<uint8_t*>(VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!code)
        return false;
    static void* record[kStandSlots];
    static void* font[kStandSlots];
    uint8_t* at = code;
    for (int i = 0; i < kStandSlots; ++i) {
        record[i] = at;
        at[0] = 0x68;
        std::memcpy(at + 1, &i, 4);
        at[5] = 0xE9;
        const int32_t rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(&RecordThunk) -
                                                 reinterpret_cast<uintptr_t>(at + 10));
        std::memcpy(at + 6, &rel, 4);
        at += 10;
    }
    for (int i = 0; i < kStandSlots; ++i) {
        font[i] = at;
        const uint8_t head[] = {0x8B, 0x49, 0x04, 0x8B, 0x01, 0xFF, 0xA0};
        std::memcpy(at, head, sizeof head);
        const uint32_t offset = static_cast<uint32_t>(i * 4);
        std::memcpy(at + 7, &offset, 4);
        at += 11;
    }
    font[kFontDraw] = record[kFontDraw];
    FlushInstructionCache(GetCurrentProcess(), code, 4096);
    g_recordTable = record;
    g_fontTable = font;
    Stand* const stands[] = {&g_standWindow, &g_standDocument, &g_standPicture, &g_standBlitter, &g_standBlit};
    const uint32_t kinds[] = {kStandWindow, kStandDocument, kStandPicture, kStandBlitter, kStandBlit};
    for (int i = 0; i < 5; ++i) {
        stands[i]->vtable = g_recordTable;
        stands[i]->kind = kinds[i];
    }
    return true;
}

// A font's stand-in (one per font of a page, numbered in the order met).
Stand* FontStand(char* real)
{
    for (int i = 0; i < g_fontCount; ++i)
        if (g_fonts[i].real == real)
            return &g_fonts[i];
    if (g_fontCount == kMaxFonts)
        return nullptr;
    Stand& s = g_fonts[g_fontCount];
    s = Stand{g_fontTable, real, kStandFont, static_cast<uint32_t>(g_fontCount), 0, 0, {}};
    ++g_fontCount;
    return &s;
}

void CollectNodes(char* node, std::vector<char*>& nodes, int depth)
{
    if (!node || depth > 64)
        return;
    nodes.push_back(node);
    const NodeClass* const c = ClassOf(At<uint32_t>(node, 0));
    if (!c || !c->container)
        return;
    char** const first = At<char**>(node, 0x1C);
    char** const last = At<char**>(node, 0x20);
    for (char** child = first; first && child < last; ++child)
        CollectNodes(*child, nodes, depth + 1);
}

// Waits for the game's clock to be in the first half of a second (phase
// even) or the second (BLINK draws in the second), 300 ms clear of the turn.
// A clock that does not move (the parity runs freeze the wall clock, and
// the game's clock with it) is left as it is: both runs read the same.
void WaitForClock(int phase)
{
    using ClockFn = uint32_t(__cdecl*)();
    const auto clock = reinterpret_cast<ClockFn>(addr::RZ_TimeMilliseconds);
    const uint32_t from = phase % 2 ? 600 : 100;
    const uint32_t first = clock();
    const ULONGLONG start = GetTickCount64();
    for (;;) {
        const uint32_t now = clock();
        const uint32_t t = now % 1000;
        if (t >= from && t < from + 300)
            return;
        const ULONGLONG waited = GetTickCount64() - start;
        if ((now == first && waited >= 50) || waited >= 2000)
            return;
        Sleep(1);
    }
}

void HashTextColours(Fnv& f, const std::vector<char*>& nodes)
{
    f.Byte(0xC0);
    for (char* node : nodes)
        if (At<uint32_t>(node, 0) == 0x00ACE300)
            HashWord(f, At<uint32_t>(node, 0x38));
}

void PaintPage(Fnv& f, char* root, char* ctx, int variant)
{
    if (!MakeStandTables())
        return;
    std::vector<char*> nodes;
    CollectNodes(root, nodes, 0);
    Fnv* const before = g_paintHash;
    g_paintHash = &f;

    // The stand-ins in place: the fonts, pictures for two of three images.
    std::vector<std::pair<char*, char*>> fonts;
    std::vector<char*> images;
    bool blink = false;
    g_fontCount = 0;
    g_sourceCount = 0;
    int image = 0;
    for (char* node : nodes) {
        switch (At<uint32_t>(node, 0)) {
        case 0x00ACE300:
            if (char* const real = At<char*>(node, 0x34))
                if (Stand* const s = FontStand(real)) {
                    fonts.emplace_back(node, real);
                    At<Stand*>(node, 0x34) = s;
                }
            break;
        case 0x00ACE188: {
            const int k = image++;
            if (k % 3 == 0 || At<char*>(node, 0x24) || g_sourceCount == kMaxSources)
                break;
            Stand& s = g_sources[g_sourceCount];
            const int32_t width = At<int32_t>(node, 0x2C) + (k % 3 == 2 ? 1 : 0);
            const int32_t height = At<int32_t>(node, 0x30);
            s = Stand{g_recordTable, nullptr, kStandSource, static_cast<uint32_t>(g_sourceCount), width, height,
                      {1, 2, width + 1, height + 2}};
            ++g_sourceCount;
            At<Stand*>(node, 0x24) = &s;
            images.push_back(node);
            break;
        }
        case 0x00ACD640:
            blink = true;
            break;
        default:
            break;
        }
    }
    const uint32_t kept[4] = {At<uint32_t>(ctx, 0x0), At<uint32_t>(ctx, 0x4), At<uint32_t>(ctx, 0x8),
                              At<uint32_t>(ctx, 0x3C)};
    At<Stand*>(ctx, 0x0) = &g_standWindow;
    At<Stand*>(ctx, 0x4) = &g_standDocument;
    At<Stand*>(ctx, 0x8) = &g_standPicture;
    At<uint32_t>(ctx, 0x3C) = 0x3C3C3C3Cu;
    g_begins = 0;
    g_bodyAnswer = nullptr;

    // Paint, three times: the view as laid out, then 150 lower, then as laid
    // out with the text runs and images marked not laid out, so that each
    // lays itself out as it is drawn (on the context's line, a cell's in its
    // copy with the cell's margins) and the new pieces are hashed.
    for (int pass = 0; pass < 3; ++pass) {
        if (pass == 1) {
            At<int32_t>(ctx, 0x20) += 150;
            At<int32_t>(ctx, 0x28) += 150;
        } else if (pass == 2) {
            At<int32_t>(ctx, 0x20) -= 150;
            At<int32_t>(ctx, 0x28) -= 150;
            for (char* node : nodes)
                if (At<uint32_t>(node, 0) == 0x00ACE300 || At<uint32_t>(node, 0) == 0x00ACE188)
                    At<uint8_t>(node, 0x9) = 0;
        }
        Static(addr::HTMLRuleNode_ColoursMade) = 0;
        if (blink)
            WaitForClock(variant + pass);
        reinterpret_cast<void(__thiscall*)(void*)>(addr::HTMLLayout_Begin)(ctx);
        f.Byte(static_cast<uint8_t>(0xD0 + pass));
        Trace("\npaint %d", pass);
        Virtual<void>(root, 0x14, ctx);
        HashContext(f, ctx);
    }
    HashLayoutNode(f, ctx, root, 0); // the pieces the third paint made

    // Hit tests on a grid over the body's box and around it.
    f.Byte(0xD7);
    Trace("\nhits", 0);
    std::vector<std::pair<char*, uint32_t>> index;
    for (std::size_t i = 0; i < nodes.size(); ++i)
        index.emplace_back(nodes[i], static_cast<uint32_t>(i));
    std::sort(index.begin(), index.end());
    const int32_t left = At<int32_t>(root, 0xC);
    const int32_t top = At<int32_t>(root, 0x10);
    const int32_t right = At<int32_t>(root, 0x14);
    const int32_t bottom = At<int32_t>(root, 0x18);
    for (int32_t y = top - 6; y < bottom + 6 && y < top + 20000; y += 9)
        for (int32_t x = left - 6; x < right + 6 && x < left + 4000; x += 11) {
            char* hit = reinterpret_cast<char*>(1);
            const int32_t answer = Virtual<int32_t>(root, 0x1C, &hit, x, y);
            const auto at = std::lower_bound(index.begin(), index.end(), std::make_pair(hit, 0u));
            const uint32_t which = at != index.end() && at->first == hit
                                       ? at->second
                                       : (hit == reinterpret_cast<char*>(1) ? 0xFFFFFFFEu : 0xFFFFFFFFu);
            f.Word(static_cast<uint32_t>(answer));
            f.Word(which);
            ++g_probes;
            if (answer) {
                Trace(" %d", static_cast<uint32_t>(answer));
                Trace(":%d", which);
            }
        }
    for (char* node : nodes)
        f.Byte(Virtual<uint8_t>(node, 0x18)); // a link or not

    // The links pressed and let go, the body given by the document every
    // second time.
    f.Byte(0xDA);
    Trace("\nlinks", 0);
    int link = 0;
    for (char* node : nodes)
        if (At<uint32_t>(node, 0) == 0x00ACE290) {
            g_bodyAnswer = link++ % 2 ? root : nullptr;
            Virtual<void>(node, 0x20, ctx);
            HashTextColours(f, nodes);
            Virtual<void>(node, 0x24, ctx);
            HashTextColours(f, nodes);
        }
    g_bodyAnswer = nullptr;

    // Back as they were.
    At<uint32_t>(ctx, 0x0) = kept[0];
    At<uint32_t>(ctx, 0x4) = kept[1];
    At<uint32_t>(ctx, 0x8) = kept[2];
    At<uint32_t>(ctx, 0x3C) = kept[3];
    for (char* node : images)
        At<char*>(node, 0x24) = nullptr;
    for (const auto& font : fonts)
        At<char*>(font.first, 0x34) = font.second;
    g_paintHash = before;
}

// The paint test's own pages, after the layout test's: text in a link in
// colours of its own and a NAME without HREF, rules of every size, BLINK
// and a rule in a coloured cell, images floating and in the line in
// coloured cells.
const char* const kPaintPages[] = {
    "<HTML><BODY LINK=\"#0000FF\" ALINK=\"#FF0000\" VLINK=\"#800080\"><A HREF=\"a.html\">plain <FONT "
    "COLOR=\"#00FF00\">green</FONT> <B>bold</B> <I><FONT COLOR=\"#123456\">deep</FONT></I></A> and <A "
    "NAME=\"here\">a name only</A> and <A HREF=\"b.html\" NAME=\"both\"><IMG SRC=\"x.png\" WIDTH=20 HEIGHT=10> "
    "picture</A> <A HREF=\"c.html\"><SPAN STYLE=\"color: #445566\">span</SPAN> end</A></BODY></HTML>",
    "<HTML><BODY><HR><HR SIZE=1><HR SIZE=2 WIDTH=40><HR SIZE=7 WIDTH=50%><TABLE><TR><TD BGCOLOR=\"#FFEEDD\">"
    "<BLINK>blink <B>in</B> a cell</BLINK><HR SIZE=3></TD><TD>x</TD></TR></TABLE><BLINK>and out</BLINK></BODY>"
    "</HTML>",
    "<HTML><BODY><TABLE><TR><TD BGCOLOR=red><IMG SRC=a.png WIDTH=30 HEIGHT=20 ALIGN=LEFT>text around a floating "
    "image in a cell that wraps more than once around it</TD><TD BGCOLOR=\"#00FF00\"><IMG SRC=b.png WIDTH=15 "
    "HEIGHT=15>i<IMG SRC=c.png WIDTH=16 HEIGHT=12 ALIGN=MIDDLE>j<IMG SRC=d.png WIDTH=10 HEIGHT=30 ALIGN=TOP></TD>"
    "</TR></TABLE><IMG SRC=e.png WIDTH=40 HEIGHT=40 ALIGN=RIGHT>text by a right float<IMG SRC=f.png WIDTH=8 "
    "HEIGHT=8></BODY></HTML>",
};

bool g_paintOn = false;
uint32_t g_paintTotal = 2166136261u;

bool AnyPaintOurs()
{
    for (const char* id : {"ui.html.nodes.paint.blocks", "ui.html.nodes.paint.text", "ui.html.nodes.paint.images",
                           "ui.html.nodes.paint.cells", "ui.html.nodes.hit", "ui.html.nodes.links"})
        if (registry::ModeOf(id) == registry::Mode::Ours)
            return true;
    return AnyLayoutOurs();
}

void DrivePaintDay(const Day& day)
{
    if (!OnRunDay(day, "OPENSC4_TEST_HTML_PAINT") || !game::IsBuild641())
        return;
    SetUpService();
    void* const face = MakeWindow("html paint");
    if (!face || !MakeStandTables())
        return;
    char* const window = static_cast<char*>(face) - 0xD8;
    const uint32_t statics[3] = {Static(addr::HTMLRuleNode_ColoursMade), Static(addr::HTMLRuleNode_Light),
                                 Static(addr::HTMLRuleNode_Dark)};

    std::size_t own = 0;
    std::vector<std::string> pages = LayoutTestPages(own);
    pages.insert(pages.begin() + static_cast<std::ptrdiff_t>(own), std::begin(kPaintPages), std::end(kPaintPages));
    own += std::size(kPaintPages);
    const bool compare = AnyPaintOurs();
    Fnv total;
    int differ = 0;
    g_paintCalls = 0;
    g_probes = 0;
    struct Variant {
        const char* name;
        const int32_t* steps;
        int32_t width;
        bool scrolled;
    };
    const Variant variants[] = {
        {"d300", nullptr, 300, true},
        {"d450", nullptr, 450, false},
        {"t300", TextPageSteps(), 300, true},
        {"t450", TextPageSteps(), 450, false},
    };
    for (std::size_t i = 0; i < pages.size(); ++i) {
        uint32_t hashes[4] = {};
        for (int v = 0; v < 4; ++v) {
            const Variant& variant = variants[v];
            auto run = [&](uint32_t& layout) {
                Fnv paint;
                layout = LayoutPage(window, pages[i], variant.steps, variant.width, variant.scrolled, &paint, v);
                return paint.h;
            };
            uint32_t gameLayout = 0;
            uint32_t game = 0;
            if (compare) {
                html_parser::g_useGame = true;
                game = run(gameLayout);
                html_parser::g_useGame = false;
            }
            uint32_t oursLayout = 0;
            const uint32_t ours = run(oursLayout);
            hashes[v] = ours;
            total.Word(ours);
            total.Word(oursLayout);
            if (compare && (game != ours || gameLayout != oursLayout)) {
                ++differ;
                log::Write("autotest: html paint: page %zu %s differs: game %08X/%08X ours %08X/%08X (%zu bytes)", i,
                           variant.name, gameLayout, game, oursLayout, ours, pages[i].size());
                if (differ <= 8) {
                    std::string trace[2];
                    for (int side = 0; side < 2; ++side) {
                        html_parser::g_useGame = side == 0;
                        g_trace = &trace[side];
                        uint32_t ignored = 0;
                        run(ignored);
                        g_trace = nullptr;
                    }
                    html_parser::g_useGame = false;
                    log::Write("autotest: html paint: page %zu %s: %s\n  game:%s\n  ours:%s", i, variant.name,
                               pages[i].c_str(), trace[0].c_str(), trace[1].c_str());
                }
            }
        }
        // OPENSC4_TEST_HTML_PAINT_TRACE=<page + 1>: that page's record (d300).
        if (static_cast<std::size_t>(EnvInt("OPENSC4_TEST_HTML_PAINT_TRACE", 0)) == i + 1) {
            std::string trace;
            g_trace = &trace;
            Fnv paint;
            const uint32_t layout =
                LayoutPage(window, pages[i], variants[0].steps, variants[0].width, variants[0].scrolled, &paint, 0);
            g_trace = nullptr;
            log::Write("autotest: html paint: page %zu d300 %08X/%08X: %s\n%s", i, layout, paint.h, pages[i].c_str(),
                       trace.c_str());
        }
        if (i < own || EnvInt("OPENSC4_TEST_HTML_PARSER_DUMP", 0))
            log::Write("autotest: html paint: page %zu %08X %08X %08X %08X", i, hashes[0], hashes[1], hashes[2],
                       hashes[3]);
    }
    Virtual<uint32_t>(face, 0x8);
    Static(addr::HTMLRuleNode_ColoursMade) = statics[0];
    Static(addr::HTMLRuleNode_Light) = statics[1];
    Static(addr::HTMLRuleNode_Dark) = statics[2];

    g_paintTotal = total.h;
    g_paintOn = true;
    log::Write("autotest: html paint: %zu pages (%zu ours, %zu ticker rows), 4 layouts each, %u stand-in calls, %u "
               "hit probes, %u stray; %s; hash %08X",
               pages.size(), own, pages.size() - own, g_paintCalls, g_probes, g_strayCalls,
               compare ? (differ ? "DIFFERENCES" : "game and ours the same") : "the game's only", g_paintTotal);
    log::Write("autotest: html paint: calls by stand-in: window %u, document %u, picture %u, image %u, stretch %u+%u, "
               "fonts %u",
               g_kindCalls[kStandWindow], g_kindCalls[kStandDocument], g_kindCalls[kStandPicture],
               g_kindCalls[kStandSource], g_kindCalls[kStandBlitter], g_kindCalls[kStandBlit], g_kindCalls[kStandFont]);
    if (compare)
        log::Write("autotest: html paint: %d differences in %zu layouts", differ, pages.size() * 4);
}

void PaintFingerprint(cISC4City*, std::string& line)
{
    if (!g_paintOn)
        return;
    char text[32];
    std::snprintf(text, sizeof text, " | hd %08X", g_paintTotal);
    line += text;
}

const FingerprintPart kPaintFingerprint{"html paint", 5, &PaintFingerprint};
const DayHook kPaintDrive{"html paint", 5, &DrivePaintDay};

// ====================================================================================
// The nodes test

// OPENSC4_TEST_HTML_NODES=<day> (fingerprint `hn`): the node constructors
// and destructors, the colours and the character helpers of h5c
// (html_nodes.cpp), each called on its own:
//
//  - each constructor on memory filled with a pattern, the object's bytes
//    hashed (a heap pointer by whether it is one the test made), then
//    deleted through its deleting destructor;
//  - each destructor on an object whose strings, pictures, fonts and
//    children are stand-ins that record their calls (slot 0, 2 or 28 and
//    the argument) and whose storage is the test's own, the object hashed
//    after and each block checked for being freed (the same blocks handed
//    out again by the game's allocator); the deleting destructors with
//    flags 1 and 0;
//  - the colour parser on every name in three cases, `#` and hex variants
//    and bad input, the body's three link colours;
//  - the character helpers at every position of ASCII and UTF-8 strings
//    (two and three bytes, broken sequences, the zero width space), the
//    prefix, start and end tests, numbers and lengths;
//  - pages of its own parsed (fonts by family lists, entities at their
//    edges, PRE's line ends, UTF-8 text), the tree hashed as the parser test
//    hashes it.
//
// With any parser group or h5c group ours, each item is run through the
// game's functions and through ours and compared.

const char* const kNodePages[] = {
    // Font families: lists, a face that is a blank, styles that ask the
    // document for a family.
    "<HTML><BODY><FONT FACE=\"NoSuchFace, Arial\">two faces</FONT><FONT FACE=\",\">blank faces</FONT>"
    "<FONT FACE=\"Arial,\">trailing comma</FONT><TT>tt <B>bold tt</B></TT><CODE>code</CODE><KBD>kbd</KBD>"
    "<PRE>pre <I>italic</I></PRE><FONT FACE=\"Times New Roman, Courier New, Arial\" SIZE=5>three</FONT>"
    "</BODY></HTML>",
    // Entities at their edges.
    "<HTML><BODY>&;&#;&#x;&#X41;&#x7a;&#255;&#256;&#-1;&#0065;&amp&amp;&&lt;&nbsp;&nbsp &#38;#38; a&b;c "
    "&unknownentity; &AMP; &Amp; &#xFFFF; &#4294967297; &;;&&&</BODY></HTML>",
    // PRE's line ends: CR, LF, CR LF, LF CR, at the ends.
    "<HTML><BODY><PRE>\r\na\rb\nc\r\nd\n\re\r\r\nf\n</PRE><PRE>\r</PRE><PRE>\n\n</PRE><PRE></PRE></BODY></HTML>",
    // Spaces of every kind and UTF-8 text.
    "<HTML><BODY>  lead\t\ttab\f\fff\r\rcr\n\nlf  trail  <B> \xC3\xA9t\xC3\xA9 \xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E"
    "\xE2\x80\x8Bzw\xE2\x80\x8B \xC3 \xFF\x80 end </B>\xC2\xA0nbsp\xC2\xA0</BODY></HTML>",
    // Colours in every attribute that takes one.
    "<HTML><BODY BGCOLOR=teal TEXT=\"#abcdef\" LINK=Fuchsia VLINK=\"#12345\" ALINK=\"lime \"><FONT COLOR=maroon>m"
    "</FONT><FONT COLOR=\"#GGGGGG\">g</FONT><FONT COLOR=\"\">e</FONT><TABLE BGCOLOR=silver><TR BGCOLOR=\"#0a0B0c\">"
    "<TD BGCOLOR=olive WIDTH=\"25%\">c</TD><TD BGCOLOR=\"#\" WIDTH=33>d</TD></TR></TABLE><HR COLOR=navy>"
    "<A HREF=\"x\">link</A></BODY></HTML>",
    // Every node class with its strings, as a page.
    "<HTML><HEAD><TITLE>t</TITLE><LINK REL=a HREF=b TYPE=c><LINK><META NAME=n CONTENT=c HTTP-EQUIV=h><META>"
    "</HEAD><BODY BACKGROUND=\"bg.png\"><A HREF=\"a.html\" NAME=\"an\">a</A><A NAME=\"only\">n</A>"
    "<IMG SRC=\"i.png\" WIDTH=4 HEIGHT=5 ALIGN=left><IMG><UL><LI>u<OL><LI>o</OL></UL>"
    "<TABLE BORDER=2 WIDTH=\"50%\" CELLPADDING=1 CELLSPACING=0><TR><TD>x</TD></TR></TABLE></BODY></HTML>",
};

// A stand-in a node owns (a string, a picture, a font, a child): each call
// of slot 0 (a child's deleting destructor), 1, 2 (AddRef and Release) or
// 28 (a string's deleting destructor) is recorded with its argument; any
// other slot ends the run.
struct NodeStand {
    void** vtable;
    uint32_t id;
};

Fnv g_standRecord;
uint32_t g_standCalls = 0;

template <int Slot>
uint32_t __fastcall StandNoArgument(NodeStand* self, void*)
{
    g_standRecord.Word(self->id);
    g_standRecord.Word(Slot);
    ++g_standCalls;
    return 1;
}

template <int Slot>
uint32_t __fastcall StandArgument(NodeStand* self, void*, uint32_t argument)
{
    g_standRecord.Word(self->id);
    g_standRecord.Word(Slot);
    g_standRecord.Word(argument);
    ++g_standCalls;
    return static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(self));
}

template <int Slot>
uint32_t __fastcall StandOther(NodeStand*, void*)
{
    log::Write("autotest: html nodes: a stand-in's slot %d was called; stopping", Slot);
    TerminateProcess(GetCurrentProcess(), 3);
    return 0;
}

template <int... Slots>
void FillStand(void** vtable, std::integer_sequence<int, Slots...>)
{
    ((vtable[Slots] = reinterpret_cast<void*>(&StandOther<Slots>)), ...);
}

void* g_nodeStandVtable[40] = {};
NodeStand g_nodeStands[32] = {};

void SetUpNodeStands()
{
    FillStand(g_nodeStandVtable, std::make_integer_sequence<int, 40>{});
    g_nodeStandVtable[0] = reinterpret_cast<void*>(&StandArgument<0>);
    g_nodeStandVtable[1] = reinterpret_cast<void*>(&StandNoArgument<1>);
    g_nodeStandVtable[2] = reinterpret_cast<void*>(&StandNoArgument<2>);
    g_nodeStandVtable[28] = reinterpret_cast<void*>(&StandArgument<28>);
    for (uint32_t i = 0; i < std::size(g_nodeStands); ++i)
        g_nodeStands[i] = {g_nodeStandVtable, 0x5700 + i};
}

uint32_t StandAt(int i) { return static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(&g_nodeStands[i])); }

void* GameNew(std::size_t size) { return reinterpret_cast<void*(__cdecl*)(std::size_t)>(addr::SC4_operator_new)(size); }
void GameDelete(void* p) { reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_operator_delete)(p); }
void* GameAllocate(std::size_t size) { return reinterpret_cast<void*(__cdecl*)(std::size_t)>(addr::SC4_stl_allocate)(size); }
void GameFree(void* p) { reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(p); }

uint32_t g_blocksChecked = 0;
uint32_t g_blocksFreed = 0;

// The blocks a test object owns: their sizes, and after its destructor
// whether the game's allocator hands each out again (it was freed).
struct Blocks {
    std::vector<void*> blocks;
    std::vector<std::size_t> sizes;
    std::vector<bool> viaNew;

    uint32_t Add(std::size_t size, bool useNew = false)
    {
        void* const p = useNew ? GameNew(size) : GameAllocate(size);
        std::memset(p, 0xAB, size);
        blocks.push_back(p);
        sizes.push_back(size);
        viaNew.push_back(useNew);
        return static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(p));
    }

    void Track(void* p, std::size_t size, bool useNew)
    {
        blocks.push_back(p);
        sizes.push_back(size);
        viaNew.push_back(useNew);
    }

    int IndexOf(uint32_t value) const
    {
        for (std::size_t i = 0; i < blocks.size(); ++i)
            if (static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(blocks[i])) == value)
                return static_cast<int>(i);
        return -1;
    }

    // Each block's size asked for again (the last first): a block freed
    // comes back. The blocks handed out are freed again; a block not freed
    // stays (the test's leak, only when a destructor fails to free).
    void HashFreed(Fnv& f)
    {
        std::vector<void*> again;
        for (std::size_t i = blocks.size(); i-- > 0;)
            again.push_back(viaNew[i] ? GameNew(sizes[i]) : GameAllocate(sizes[i]));
        for (void* p : blocks) {
            const bool freed = std::find(again.begin(), again.end(), p) != again.end();
            f.Byte(freed ? 1 : 0);
            ++g_blocksChecked;
            g_blocksFreed += freed ? 1 : 0;
        }
        for (std::size_t j = 0; j < again.size(); ++j) {
            const std::size_t i = blocks.size() - 1 - j;
            if (viaNew[i])
                GameDelete(again[j]);
            else
                GameFree(again[j]);
        }
    }
};

bool IsHeap(uint32_t value)
{
    if (value < 0x00010000u || value >= 0x80000000u)
        return false;
    MEMORY_BASIC_INFORMATION info{};
    return VirtualQuery(reinterpret_cast<void*>(static_cast<uintptr_t>(value)), &info, sizeof info) &&
           info.State == MEM_COMMIT && info.Type == MEM_PRIVATE;
}

// An object's bytes, dword by dword: a stand-in by its number, a block of
// the test's own by its index, any other heap pointer as one, the rest as
// they are (the vtables are the game's).
void HashObject(Fnv& f, const char* object, std::size_t size, const Blocks* blocks)
{
    for (std::size_t at = 0; at + 4 <= size; at += 4) {
        const uint32_t value = *reinterpret_cast<const uint32_t*>(object + at);
        const int index = blocks ? blocks->IndexOf(value) : -1;
        const uint32_t first = StandAt(0);
        if (value >= first && value < first + sizeof g_nodeStands && (value - first) % sizeof(NodeStand) == 0) {
            f.Byte(0xB3);
            f.Word((value - first) / static_cast<uint32_t>(sizeof(NodeStand)));
        } else if (index >= 0) {
            f.Byte(0xB1);
            f.Word(static_cast<uint32_t>(index));
        } else if (IsHeap(value)) {
            f.Byte(0xB2);
        } else {
            f.Word(value);
        }
    }
}

template <typename R, typename... A>
R CallGameThis(std::uintptr_t fn, void* self, A... a)
{
    return reinterpret_cast<R(__thiscall*)(void*, A...)>(fn)(self, a...);
}

constexpr uint8_t kPattern = 0xCD;

// The constructors on patterned memory, then the deleting destructor.
uint32_t ConstructorItem(int which)
{
    struct Ctor {
        std::uintptr_t fn;
        uint32_t size;
        int arguments;
        uint32_t a[3];
    };
    const Ctor ctors[] = {
        {addr::HTMLNode_Construct, 0x1C, 1, {0x55}},
        {addr::HTMLImageNode_Construct, 0x38, 0, {}},
        {addr::HTMLHeadLinkNode_Construct, 0x2C, 0, {}},
        {addr::HTMLMetaNode_Construct, 0x28, 0, {}},
        {addr::HTMLListNode_Construct, 0x2C, 2, {6, 3}},
        {addr::HTMLListNode_Construct, 0x2C, 2, {7, 0}},
        {addr::HTMLTableNode_Construct, 0x94, 0, {}},
        {addr::HTMLCellNode_Construct, 0x3C, 0, {}},
        {addr::HTMLLinkNode_Construct, 0x34, 0, {}},
        {addr::HTMLBodyNode_Construct, 0x44, 3, {0x81223344, 0x95667788, 0xA9AABBCC}},
    };
    const Ctor& c = ctors[which];
    char* const object = static_cast<char*>(GameNew(c.size));
    std::memset(object, kPattern, c.size);
    char* result = nullptr;
    switch (c.arguments) {
    case 0: result = CallGameThis<char*>(c.fn, object); break;
    case 1: result = CallGameThis<char*>(c.fn, object, c.a[0]); break;
    case 2: result = CallGameThis<char*>(c.fn, object, c.a[0], c.a[1]); break;
    default: result = CallGameThis<char*>(c.fn, object, c.a[0], c.a[1], c.a[2]); break;
    }
    Fnv f;
    f.Byte(result == object ? 1 : 0);
    HashObject(f, object, c.size, nullptr);
    // A field the constructor left as it was (LINK's +0x28) cleared before
    // the destructor reads it.
    for (std::size_t at = 0; at + 4 <= c.size; at += 4)
        if (At<uint32_t>(object, at) == 0xCDCDCDCDu)
            At<uint32_t>(object, at) = 0;
    Virtual<char*>(object, 0x0, uint32_t{1});
    return f.h;
}

constexpr int kConstructorItems = 10;

// The destructors: an object made by its constructor (a text run by hand),
// its fields given stand-ins and blocks, then the destructor (way 0), the
// deleting one with flags 1 (way 1) or with 0 (way 2).
uint32_t DestructorItem(int which, int way)
{
    struct Dtor {
        std::uintptr_t construct; // 0: a text run, made by hand
        std::uintptr_t destruct;
        std::uintptr_t remove;
        uint32_t size;
    };
    const Dtor dtors[] = {
        {addr::HTMLContainerNode_Construct, addr::HTMLContainerNode_Destruct, addr::HTMLContainerNode_Delete, 0x28},
        {addr::HTMLImageNode_Construct, addr::HTMLImageNode_Destruct, addr::HTMLImageNode_Delete, 0x38},
        {addr::HTMLHeadLinkNode_Construct, addr::HTMLHeadLinkNode_Destruct, addr::HTMLHeadLinkNode_Delete, 0x2C},
        {addr::HTMLMetaNode_Construct, addr::HTMLMetaNode_Destruct, addr::HTMLMetaNode_Delete, 0x28},
        {addr::HTMLLinkNode_Construct, addr::HTMLLinkNode_Destruct, addr::HTMLLinkNode_Delete, 0x34},
        {addr::HTMLBodyNode_Construct, addr::HTMLBodyNode_Destruct, addr::HTMLBodyNode_Delete, 0x44},
        {addr::HTMLTableNode_Construct, addr::HTMLTableNode_Destruct, addr::HTMLTableNode_Delete, 0x94},
        {0, addr::HTMLTextNode_Destruct, addr::HTMLTextNode_Delete, 0x58},
    };
    const Dtor& d = dtors[which];
    char* const object = static_cast<char*>(GameNew(d.size));
    std::memset(object, kPattern, d.size);
    Blocks blocks;
    if (which == 0)
        CallGameThis<char*>(d.construct, object, uint32_t{8});
    else if (which == 5)
        CallGameThis<char*>(d.construct, object, uint32_t{1}, uint32_t{2}, uint32_t{3});
    else if (d.construct)
        CallGameThis<char*>(d.construct, object);
    // A container's children: stand-ins and a null among them.
    auto children = [&](std::initializer_list<int> stands) {
        const uint32_t storage = blocks.Add(4 * stands.size());
        uint32_t* const at = reinterpret_cast<uint32_t*>(static_cast<uintptr_t>(storage));
        std::size_t n = 0;
        for (int s : stands)
            at[n++] = s < 0 ? 0 : StandAt(s);
        At<uint32_t>(object, 0x1C) = storage;
        At<uint32_t>(object, 0x20) = storage + static_cast<uint32_t>(4 * n);
        At<uint32_t>(object, 0x24) = storage + static_cast<uint32_t>(4 * n);
    };
    auto vector = [&](std::size_t at, std::size_t bytes) {
        const uint32_t storage = bytes ? blocks.Add(bytes) : 0;
        At<uint32_t>(object, at) = storage;
        At<uint32_t>(object, at + 4) = storage;
        At<uint32_t>(object, at + 8) = storage + static_cast<uint32_t>(bytes);
    };
    switch (which) {
    case 0: children({0, -1, 1, 2}); break;
    case 1: At<uint32_t>(object, 0x24) = StandAt(3); At<uint32_t>(object, 0x28) = StandAt(4); break;
    case 2: At<uint32_t>(object, 0x1C) = StandAt(5); At<uint32_t>(object, 0x28) = StandAt(6);
            At<uint32_t>(object, 0x24) = 0; break;
    case 3: At<uint32_t>(object, 0x1C) = StandAt(7); At<uint32_t>(object, 0x20) = StandAt(8);
            At<uint32_t>(object, 0x24) = StandAt(9); break;
    case 4: children({10}); At<uint32_t>(object, 0x28) = StandAt(11); At<uint32_t>(object, 0x30) = StandAt(12); break;
    case 5: children({13, 14}); At<uint32_t>(object, 0x2C) = blocks.Add(12, true);
            At<uint32_t>(object, 0x3C) = StandAt(15); break;
    case 6: children({16}); vector(0x70, 28); vector(0x7C, 40); vector(0x88, 12); vector(0x4C, 20); vector(0x58, 0);
            vector(0x64, 44); break;
    case 7:
        std::memset(object, kPattern, d.size);
        At<uint32_t>(object, 0x0) = 0x00ACE300;
        At<uint32_t>(object, 0x1C) = 0x00ACE2EC;
        MakeText(reinterpret_cast<GameText*>(object + 0x20), "a run of text");
        At<uint32_t>(object, 0x34) = StandAt(17);
        vector(0x4C, 48);
        break;
    }
    Fnv f;
    g_standRecord = Fnv{};
    if (way == 0)
        CallGameThis<void>(d.destruct, object);
    else
        f.Byte(CallGameThis<char*>(d.remove, object, uint32_t{way == 1 ? 1u : 0u}) == object ? 1 : 0);
    f.Word(g_standRecord.h);
    if (way != 1)
        HashObject(f, object, d.size, &blocks);
    else
        blocks.Track(object, d.size, true);
    blocks.HashFreed(f);
    if (way != 1)
        GameDelete(object);
    return f.h;
}

constexpr int kDestructorItems = 8 * 3;

// The colour parser: each input into a colour set beforehand.
const char* const kColourInputs[] = {
    "", "#", "##", "#000000", "#FFFFFF", "#ffffff", "#00ff7F", "00FF7f", "abcdef", "#0F0", "#1234567", "#12345G",
    "#12345", " #123456", "#123456 ", "#-12345", "#+12345", "0x1234", "#0x1234", "red ", " red", "Red", "rEd", "#red",
    "GRAYish", "grey", "Gray", "#C0C0C0", "\xC3\xA9\xC3\xA9\xC3\xA9", "123456", "\x80\x81\x82\x83\x84\x85", "#ABCDEF00",
};

uint32_t ColourItem(int which)
{
    static std::vector<std::string> inputs = [] {
        std::vector<std::string> all(std::begin(kColourInputs), std::end(kColourInputs));
        const char* const names[] = {"BLACK", "SILVER", "GRAY", "WHITE", "MAROON", "RED", "PURPLE", "FUCHSIA",
                                     "GREEN", "LIME", "OLIVE", "YELLOW", "NAVY", "BLUE", "TEAL", "AQUA"};
        for (const char* name : names) {
            std::string upper = name, lower = name, mixed = name;
            for (std::size_t i = 0; i < upper.size(); ++i) {
                lower[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(lower[i])));
                if (i % 2)
                    mixed[i] = lower[i];
            }
            all.push_back(upper);
            all.push_back(lower);
            all.push_back(mixed);
        }
        return all;
    }();
    if (static_cast<std::size_t>(which) >= inputs.size())
        return 0;
    GameText text;
    MakeText(&text, inputs[static_cast<std::size_t>(which)].c_str());
    Fnv f;
    for (const uint32_t start : {0x12345678u, 0u}) {
        uint32_t colour = start;
        reinterpret_cast<void(__cdecl*)(const GameText*, uint32_t*)>(addr::HTMLColor_Parse)(&text, &colour);
        f.Word(colour);
    }
    // The body's link colours, into a body made by its constructor.
    char* const body = static_cast<char*>(GameNew(0x44));
    std::memset(body, kPattern, 0x44);
    CallGameThis<char*>(addr::HTMLBodyNode_Construct, body, uint32_t{1}, uint32_t{2}, uint32_t{3});
    const std::uintptr_t setters[] = {addr::HTMLBodyNode_SetLink, addr::HTMLBodyNode_SetActiveLink,
                                      addr::HTMLBodyNode_SetVisitedLink};
    CallGameThis<void>(setters[which % 3], body, static_cast<const GameText*>(&text));
    for (const std::size_t at : {0x30u, 0x34u, 0x38u})
        HashWord(f, At<uint32_t>(body, at));
    Virtual<char*>(body, 0x0, uint32_t{1});
    DestroyText(&text);
    return f.h;
}

int ColourItems() { return static_cast<int>(std::size(kColourInputs)) + 16 * 3; }

// The character helpers over strings of every kind.
const char* const kCharStrings[] = {
    "",
    "a",
    " ",
    "  a  b\t\tc\r\nd\fe\vf  ",
    "h\xC3\xA9llo w\xC3\xB6rld",
    "\xE6\x97\xA5\xE6\x9C\xAC \xE8\xAA\x9E",
    "\x80\xFF\xC3",
    "x\xE2\x80\x8By \xE2\x80\x8B",
    "\xC3",
    "\xF0\x9F\x98\x80 four",
    "#x41;",
    "#X7a",
    "50%",
    "%",
    "100%%",
    "7px",
    " 3",
    "-12",
    "4294967296",
    "abc",
};

uint32_t CharItem(int which)
{
    std::string s;
    if (which < static_cast<int>(std::size(kCharStrings))) {
        s = kCharStrings[which];
    } else {
        for (int c = 1; c < 128; ++c)
            s += static_cast<char>(c);
    }
    GameText text;
    MakeText(&text, s.c_str());
    Fnv f;
    const uint32_t n = static_cast<uint32_t>(text.end - text.begin);
    using PositionFn = uint32_t(__cdecl*)(const GameText*, uint32_t);
    using TestFn = uint8_t(__cdecl*)(const GameText*, uint32_t);
    for (uint32_t i = 0; i <= n; ++i) {
        if (i < n)
            f.Word(reinterpret_cast<PositionFn>(addr::HTMLText_NextChar)(&text, i));
        if (i > 0)
            f.Word(reinterpret_cast<PositionFn>(addr::HTMLText_PreviousChar)(&text, i));
        f.Byte(reinterpret_cast<TestFn>(addr::HTMLText_IsSpaceAt)(&text, i) & 1);
        for (const int32_t left : {static_cast<int32_t>(n - i), 0, -1, 1})
            f.Byte(reinterpret_cast<uint8_t(__cdecl*)(const char*, int32_t)>(addr::HTMLText_IsSpace)(text.begin + i,
                                                                                                   left) &
                   1);
    }
    using PrefixFn = uint8_t(__cdecl*)(const GameText*, uint32_t, const char*);
    for (const char* prefix : {"#x", "#X", "", "abc", "a", "  "})
        for (uint32_t count = 0; count <= 3; ++count)
            f.Byte(reinterpret_cast<PrefixFn>(addr::HTMLText_HasPrefix)(&text, count, prefix) & 1);
    using CharFn = uint32_t(__cdecl*)(const GameText*, char);
    for (const char c : {'#', '%', 'a', ' ', '\0', '\xC3'}) {
        f.Word(reinterpret_cast<CharFn>(addr::HTMLText_StartsWith)(&text, c));
        f.Word(reinterpret_cast<CharFn>(addr::HTMLText_EndsWith)(&text, c));
    }
    uint32_t number = 0x5A5A5A5A;
    reinterpret_cast<void(__cdecl*)(const GameText*, uint32_t*)>(addr::HTMLText_ParseNumber)(&text, &number);
    f.Word(number);
    uint32_t length[2] = {0x5A5A5A5A, 0x5A5A5A5A};
    reinterpret_cast<void(__cdecl*)(const GameText*, uint32_t*)>(addr::HTMLText_ParseLength)(&text, length);
    f.Word(length[0]);
    f.Word(length[1]);
    DestroyText(&text);
    return f.h;
}

int CharItems() { return static_cast<int>(std::size(kCharStrings)) + 1; }

bool g_nodesOn = false;
uint32_t g_nodesHash = 2166136261u;

void DriveNodesDay(const Day& day)
{
    if (!OnRunDay(day, "OPENSC4_TEST_HTML_NODES") || !game::IsBuild641())
        return;
    SetUpService();
    SetUpNodeStands();
    void* const face = MakeWindow("html nodes");
    if (!face)
        return;
    char* const window = static_cast<char*>(face) - 0xD8;
    const bool compare = AnyOurs();
    Fnv total;
    int items = 0;
    int differ = 0;
    uint32_t sectionHash[5] = {};
    const char* const sections[] = {"constructors", "destructors", "colours", "characters", "pages"};
    // OPENSC4_TEST_HTML_NODES_TRACE=1: each item logged before it runs.
    const bool trace = EnvInt("OPENSC4_TEST_HTML_NODES_TRACE", 0) != 0;
    auto item = [&](int section, int index, auto run) {
        if (trace)
            log::Write("autotest: html nodes: %s %d", sections[section], index);
        uint32_t game = 0;
        uint32_t gameIdentity = 0;
        if (compare) {
            html_parser::g_useGame = true;
            g_identity = Fnv{};
            game = run();
            gameIdentity = g_identity.h;
            html_parser::g_useGame = false;
        }
        g_identity = Fnv{};
        const uint32_t ours = run();
        ++items;
        if (trace)
            log::Write("autotest: html nodes: %s %d: %08X", sections[section], index, ours);
        total.Word(ours);
        Fnv s;
        s.Word(sectionHash[section]);
        s.Word(ours);
        sectionHash[section] = s.h;
        if (compare && (game != ours || gameIdentity != g_identity.h)) {
            ++differ;
            log::Write("autotest: html nodes: %s %d differs: game %08X ours %08X", sections[section], index, game,
                       ours);
        }
    };
    for (int i = 0; i < kConstructorItems; ++i)
        item(0, i, [&] { return ConstructorItem(i); });
    for (int i = 0; i < kDestructorItems; ++i)
        item(1, i, [&] { return DestructorItem(i / 3, i % 3); });
    for (int i = 0; i < ColourItems(); ++i)
        item(2, i, [&] { return ColourItem(i); });
    for (int i = 0; i < CharItems(); ++i)
        item(3, i, [&] { return CharItem(i); });
    for (std::size_t i = 0; i < std::size(kNodePages); ++i)
        item(4, static_cast<int>(i), [&] {
            bool parsed = false;
            return ParsePage(window, kNodePages[i], parsed) ^ (parsed ? 0 : 0x80000000u);
        });
    Virtual<uint32_t>(face, 0x8);

    g_nodesHash = total.h;
    g_nodesOn = true;
    log::Write("autotest: html nodes: %d items (constructors %08X, destructors %08X, colours %08X, characters %08X, "
               "pages %08X), %u stand-in calls, %u of %u blocks freed; %s; hash %08X",
               items, sectionHash[0], sectionHash[1], sectionHash[2], sectionHash[3], sectionHash[4], g_standCalls,
               g_blocksFreed, g_blocksChecked,
               compare ? (differ ? "DIFFERENCES" : "game and ours the same") : "the game's only", g_nodesHash);
    if (compare)
        log::Write("autotest: html nodes: %d differences in %d items", differ, items);
}

void NodesFingerprint(cISC4City*, std::string& line)
{
    if (!g_nodesOn)
        return;
    char text[32];
    std::snprintf(text, sizeof text, " | hn %08X", g_nodesHash);
    line += text;
}

const FingerprintPart kNodesFingerprint{"html nodes", 5, &NodesFingerprint};
const DayHook kNodesDrive{"html nodes", 5, &DriveNodesDay};

} // namespace
} // namespace opensc4::autotest
