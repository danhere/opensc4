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
    for (const char* id : {"ui.html.parser.tokens", "ui.html.parser.names", "ui.html.parser.document", "ui.html.parser.blocks", "ui.html.parser.table", "ui.html.parser.inline"})
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

// One page parsed (with the font steps given, or the default ones) and laid
// out `width` wide; the hash of the boxes.
uint32_t LayoutPage(char* window, const std::string& page, const int32_t* steps, int32_t width, bool scrolled)
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
                           "ui.html.nodes.layout.images", "ui.html.nodes.layout.cells"})
        if (registry::ModeOf(id) == registry::Mode::Ours)
            return true;
    return AnyOurs();
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

    std::vector<std::string> mine(std::begin(kPages), std::end(kPages));
    mine.insert(mine.end(), std::begin(kLayoutPages), std::end(kLayoutPages));
    std::size_t own = 0;
    const std::vector<std::string> pages = AllPages(std::move(mine), own);

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

} // namespace
} // namespace opensc4::autotest
