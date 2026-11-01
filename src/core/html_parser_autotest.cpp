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
// The requests cancelled (the images section hashes them).
Fnv g_serviceCancels;
uint8_t __fastcall ServiceCancel(Service*, void*, uint32_t request)
{
    g_serviceCancels.Word(request);
    return 1;
}
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
                           "ui.html.chars", "ui.html.nodes.pictures", "ui.html.nodes.children",
                           "ui.html.document.ctors", "ui.html.document.dtors", "ui.html.document.attributes",
                           "ui.html.document.handlers", "ui.html.document.tokenizer", "ui.html.document.slots",
                           "ui.html.document.life", "ui.html.document.glue", "ui.html.document.fonts",
                           "ui.html.document.images", "ui.html.document.imagemap", "ui.html.document.bullets",
                           "ui.html.document.vslots", "ui.html.window.life", "ui.html.window.show",
                           "ui.html.window.slots", "ui.html.window.settings", "ui.html.window.input",
                           "ui.html.window.scroll", "ui.html.window.paint", "ui.html.window.address",
                           "ui.html.directors", "ui.html.block", "ui.html.cache.map", "ui.html.cache.entries"})
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
    // h1b's (the window's input, scroll bars and paint)
    kStandBar = 9,
    kStandBarWindow,
    kStandBarPicture,
    kStandFactory,
    kStandOther,
    kStandCapture,
    kStandCanvas,
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
    case kStandBar:
        switch (slot) {
        case 2:
        case 3:
        case 6:
            return ""; // release, its window, its position
        case 5:
            return "i"; // the position
        case 7:
        case 15:
            return "ii"; // the range, the steps
        case 17:
            return "roi"; // the picture and its part
        default:
            return nullptr;
        }
    case kStandBarWindow:
        return slot == 41 || slot == 42 || slot == 63 ? "" : (slot == 55 ? "iiii" : nullptr);
    case kStandBarPicture:
        return slot == 1 || slot == 2 || slot == 12 ? "" : nullptr;
    case kStandFactory:
        return slot == 17 ? "iiiii" : nullptr;
    case kStandOther:
        return slot == 92 ? "" : nullptr;
    case kStandCapture:
        return slot == 28 ? "" : nullptr;
    case kStandCanvas:
        switch (slot) {
        case 0:
            return "ip";
        case 6:
        case 7:
            return "i";
        case 20:
            return "irr";
        case 28:
            return "iiiiii";
        case 29:
            return "orrr";
        default:
            return nullptr;
        }
    default:
        return nullptr;
    }
}

// h1b's stand-ins: two scroll bars, their windows, two bar pictures, the
// window factory, +0x48, the window manager at +4 (who has the mouse) and
// the window's picture (+0x68), each with the recording vtable.
constexpr int kInputStands = 12;
Stand g_inputStands[kInputStands];
Stand* const g_inputBars = &g_inputStands[0];        // 2
Stand* const g_inputBarWindows = &g_inputStands[2];  // 2
Stand* const g_inputBarPictures = &g_inputStands[4]; // 2
Stand& g_inputFactory = g_inputStands[6];
Stand& g_inputOther = g_inputStands[7];
Stand& g_inputCapture = g_inputStands[8];
Stand& g_inputCanvas = g_inputStands[9];
uint32_t g_factoryBars = 0;      // the bars the factory gives (bit 0 the first, bit 1 the second)
uint32_t g_factoryMade = 0;      // how many it was asked for
uint32_t g_canvasRefuses = 0;    // the lock the canvas refuses (slot 6)
char* g_captured = nullptr;      // the window the manager says has the mouse

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
    if (p >= g_inputStands && p < g_inputStands + kInputStands)
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
        log::Write("autotest: html paint: stand-in %u slot %u not known (from %08X)", self->kind, slot, args[-1]);
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
    case kStandBar:
        // Its window, the position it was given (+0x10, `width`).
        if (slot == 3)
            answer = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&g_inputBarWindows[self->id]));
        else if (slot == 5)
            self->width = static_cast<int32_t>(args[0]);
        else if (slot == 6)
            answer = static_cast<uint32_t>(self->width);
        break;
    case kStandBarWindow:
        answer = slot == 41 ? 16 + self->id : (slot == 42 ? 12 + self->id : (slot == 63 ? 0x6300 + self->id : 1));
        break;
    case kStandBarPicture:
        answer = slot == 12 ? static_cast<uint32_t>(reinterpret_cast<uintptr_t>(self->area)) : 1;
        break;
    case kStandFactory:
        answer = 0;
        if (g_factoryBars & (1u << g_factoryMade))
            answer = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&g_inputBars[g_factoryMade]));
        g_factoryMade = (g_factoryMade + 1) % 2;
        break;
    case kStandCapture:
        answer = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(g_captured));
        break;
    case kStandCanvas:
        answer = slot == 0 ? 0 : (slot == 6 ? (args[0] == g_canvasRefuses ? 0 : 1) : 1);
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
// of slot 0 (a child's deleting destructor), 1, 2 (AddRef and Release), 9,
// 10 (a picture's width and height) or 28 (a string's deleting destructor)
// is recorded with its argument; any other slot ends the run.
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

// A picture's width and height (slots 9 and 10): from its number, 0 for
// every third stand-in.
template <int Slot>
uint32_t __fastcall StandSize(NodeStand* self, void*)
{
    g_standRecord.Word(self->id);
    g_standRecord.Word(Slot);
    ++g_standCalls;
    return (self->id & 0xFF) % 3 * (Slot == 9 ? 7u : 11u);
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
    g_nodeStandVtable[9] = reinterpret_cast<void*>(&StandSize<9>);
    g_nodeStandVtable[10] = reinterpret_cast<void*>(&StandSize<10>);
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

// The pictures set (slot 0 of the body's and IMG's image items), the
// containers' AddChild (slot 1) and TABLE's WIDTH, each on an object made by
// its constructor: the object hashed after, with the stand-ins' calls and
// what the function returned.
const char* const kWidthInputs[] = {"", "50%", "33", "abc", "7px", "%", "100%%", " 3"};

constexpr int kBodyPictureItems = 3;
constexpr int kImagePictureItems = 4 * 3 + 1;
constexpr int kChildItems = 6;

uint32_t ChildrenItem(int which)
{
    Fnv f;
    g_standRecord = Fnv{};
    if (which < kBodyPictureItems) {
        // The body's: none, one picture, then a second over it.
        char* const body = static_cast<char*>(GameNew(0x44));
        std::memset(body, kPattern, 0x44);
        CallGameThis<char*>(addr::HTMLBodyNode_Construct, body, uint32_t{1}, uint32_t{2}, uint32_t{3});
        const std::vector<uint32_t> pictures[] = {{0}, {StandAt(18)}, {StandAt(19), StandAt(20)}};
        for (const uint32_t picture : pictures[which])
            f.Byte(CallGameThis<uint8_t>(addr::HTMLBodyNode_SetPicture, body + 0x28, picture));
        HashObject(f, body, 0x44, nullptr);
        f.Word(g_standRecord.h);
        Virtual<char*>(body, 0x0, uint32_t{1});
        return f.h;
    }
    which -= kBodyPictureItems;
    if (which < kImagePictureItems) {
        // IMG's: WIDTH and HEIGHT given or not, with no picture, a picture
        // with a size and one of 0 by 0; then two pictures, one over the
        // other.
        char* const image = static_cast<char*>(GameNew(0x38));
        std::memset(image, kPattern, 0x38);
        CallGameThis<char*>(addr::HTMLImageNode_Construct, image);
        const uint32_t sizes[][2] = {{0, 0}, {4, 0}, {0, 5}, {4, 5}};
        std::vector<uint32_t> pictures;
        if (which < 12) {
            At<uint32_t>(image, 0x2C) = sizes[which / 3][0];
            At<uint32_t>(image, 0x30) = sizes[which / 3][1];
            const uint32_t choice[] = {0, StandAt(22 + which / 3), StandAt(21)};
            pictures.push_back(choice[which % 3]);
        } else {
            pictures = {StandAt(26), StandAt(28)};
        }
        for (const uint32_t picture : pictures)
            f.Word(CallGameThis<uint32_t>(addr::HTMLImageNode_SetPicture, image + 0x20, picture));
        HashObject(f, image, 0x38, nullptr);
        f.Word(g_standRecord.h);
        Virtual<char*>(image, 0x0, uint32_t{1});
        return f.h;
    }
    which -= kImagePictureItems;
    if (which < kChildItems) {
        // A container's children: 1, 2, 3, 5 and 9 of them (the vector grown
        // on the way), then 4 with a null among them; what AddChild returned
        // by where it points in the children's storage.
        char* const container = static_cast<char*>(GameNew(0x28));
        std::memset(container, kPattern, 0x28);
        CallGameThis<char*>(addr::HTMLContainerNode_Construct, container, uint32_t{8});
        const int counts[] = {1, 2, 3, 5, 9, 4};
        std::vector<uint32_t> returned;
        for (int i = 0; i < counts[which]; ++i) {
            const uint32_t child = which == 5 && i == 1 ? 0 : StandAt(i);
            returned.push_back(CallGameThis<uint32_t>(addr::HTMLContainerNode_AddChild, container, child));
        }
        const uint32_t begin = At<uint32_t>(container, 0x1C);
        const uint32_t end = At<uint32_t>(container, 0x20);
        const uint32_t storage = At<uint32_t>(container, 0x24);
        f.Word((end - begin) / 4);
        f.Word((storage - begin) / 4);
        for (const uint32_t r : returned)
            f.Word(r >= begin && r <= storage ? 0xC0000000u | (r - begin) : IsHeap(r) ? 0xB2u : r);
        HashObject(f, reinterpret_cast<const char*>(static_cast<uintptr_t>(begin)), end - begin, nullptr);
        HashObject(f, container, 0x1C, nullptr);
        f.Word(g_standRecord.h);
        Virtual<char*>(container, 0x0, uint32_t{1});
        return f.h;
    }
    which -= kChildItems;
    // TABLE's WIDTH.
    char* const table = static_cast<char*>(GameNew(0x94));
    std::memset(table, kPattern, 0x94);
    CallGameThis<char*>(addr::HTMLTableNode_Construct, table);
    GameText text;
    MakeText(&text, kWidthInputs[which]);
    CallGameThis<void>(addr::HTMLTableNode_SetWidth, table, static_cast<const GameText*>(&text));
    DestroyText(&text);
    HashObject(f, table, 0x4C, nullptr);
    Virtual<char*>(table, 0x0, uint32_t{1});
    return f.h;
}

int ChildrenItems()
{
    return kBodyPictureItems + kImagePictureItems + kChildItems + static_cast<int>(std::size(kWidthInputs));
}

// h2a's items: the document's node constructors and destructors, attribute
// setters, small handlers and tokenizer helpers (html_document.cpp), each
// on an object of its own or on pages of the test's own.

// The constructors outside h5 on patterned memory, then slot 0.
uint32_t DocConstructorItem(int which)
{
    struct Ctor {
        std::uintptr_t fn;
        uint32_t size;
    };
    const Ctor ctors[] = {
        {addr::HTMLBreakNode_Construct, 0x1C},     {addr::HTMLRuleNode_Construct, 0x2C},
        {addr::HTMLInputNode_Construct, 0x1C},     {addr::HTMLContainerNode_Construct, 0x28},
        {addr::HTMLAreaNode_Construct, 0x2C},      {addr::HTMLBlinkNode_Construct, 0x28},
        {addr::HTMLQuoteNode_Construct, 0x28},     {addr::HTMLDDNode_Construct, 0x28},
        {addr::HTMLAlignNode_Construct, 0x28},     {addr::HTMLDTNode_Construct, 0x28},
        {addr::HTMLFormNode_Construct, 0x28},      {addr::HTMLHeadingNode_Construct, 0x2C},
        {addr::HTMLItemNode_Construct, 0x28},      {addr::HTMLItemBodyNode_Construct, 0x28},
        {addr::HTMLMapNode_Construct, 0x2C},       {addr::HTMLOptionNode_Construct, 0x28},
        {addr::HTMLParagraphNode_Construct, 0x28}, {addr::HTMLSelectNode_Construct, 0x28},
        {addr::HTMLSpanNode_Construct, 0x28},      {addr::HTMLRowNode_Construct, 0x3C},
        {addr::HTMLHeaderCellNode_Construct, 0x28},
    };
    const Ctor& c = ctors[which];
    char* const object = static_cast<char*>(GameNew(c.size));
    std::memset(object, kPattern, c.size);
    char* const result = c.fn == addr::HTMLContainerNode_Construct
                             ? CallGameThis<char*>(c.fn, object, uint32_t{0x55})
                             : CallGameThis<char*>(c.fn, object);
    Fnv f;
    f.Byte(result == object ? 1 : 0);
    HashObject(f, object, c.size, nullptr);
    for (std::size_t at = 0; at + 4 <= c.size; at += 4)
        if (At<uint32_t>(object, at) == 0xCDCDCDCDu)
            At<uint32_t>(object, at) = 0;
    Virtual<char*>(object, 0x0, uint32_t{1});
    return f.h;
}

constexpr int kDocConstructorItems = 21;

// The leaves' deleting destructor (BR, HR and INPUT, flags 1 and 0) and TR's
// destructor and deleting destructor (flags 1 and 0) on a row with cells'
// storage and children of the test's own.
uint32_t DocDestructorItem(int which)
{
    Fnv f;
    g_standRecord = Fnv{};
    Blocks blocks;
    if (which < 4) {
        const std::uintptr_t ctors[] = {addr::HTMLBreakNode_Construct, addr::HTMLRuleNode_Construct,
                                        addr::HTMLInputNode_Construct, addr::HTMLBreakNode_Construct};
        const uint32_t sizes[] = {0x1C, 0x2C, 0x1C, 0x1C};
        char* const object = static_cast<char*>(GameNew(sizes[which]));
        std::memset(object, kPattern, sizes[which]);
        CallGameThis<char*>(ctors[which], object);
        const uint32_t flags = which == 3 ? 0u : 1u;
        f.Byte(Virtual<char*>(object, 0x0, flags) == object ? 1 : 0);
        if (flags) {
            blocks.Track(object, sizes[which], true);
        } else {
            HashObject(f, object, sizes[which], nullptr);
            GameDelete(object);
        }
        blocks.HashFreed(f);
        return f.h;
    }
    const int way = which - 4;
    char* const row = static_cast<char*>(GameNew(0x3C));
    std::memset(row, kPattern, 0x3C);
    CallGameThis<char*>(addr::HTMLRowNode_Construct, row);
    const uint32_t storage = blocks.Add(12);
    uint32_t* const children = reinterpret_cast<uint32_t*>(static_cast<uintptr_t>(storage));
    children[0] = StandAt(29);
    children[1] = 0;
    children[2] = StandAt(30);
    At<uint32_t>(row, 0x1C) = storage;
    At<uint32_t>(row, 0x20) = storage + 12;
    At<uint32_t>(row, 0x24) = storage + 12;
    if (way != 1) {
        const uint32_t cells = blocks.Add(8);
        At<uint32_t>(row, 0x28) = cells;
        At<uint32_t>(row, 0x2C) = cells + 4;
        At<uint32_t>(row, 0x30) = cells + 8;
    }
    if (way == 0)
        CallGameThis<void>(addr::HTMLRowNode_Destruct, row);
    else
        f.Byte(CallGameThis<char*>(addr::HTMLRowNode_Delete, row, uint32_t{way == 1 ? 1u : 0u}) == row ? 1 : 0);
    f.Word(g_standRecord.h);
    if (way != 1)
        HashObject(f, row, 0x3C, &blocks);
    else
        blocks.Track(row, 0x3C, true);
    blocks.HashFreed(f);
    if (way != 1)
        GameDelete(row);
    return f.h;
}

constexpr int kDocDestructorItems = 7;

// The attribute setters' inputs.
const char* const kDocColours[] = {"red", "#00ff7f", "", "#", "Teal", "#12345", "nothing", "#ABCDEF"};
const char* const kDocAligns[] = {"left", "LEFT", "Right", "center", "CENTER ", "top", "Top", "bottom", "middle",
                                  "MIDDLE", "justify", "", " left", "\xC3\xA9", "absmiddle", "baseline"};
const char* const kDocStrings[] = {"", "a.html", "Name with spaces", "\xC3\xA9t\xC3\xA9", "nowrap"};

constexpr int kDocBackgroundItems = static_cast<int>(std::size(kDocColours)) * 3;
constexpr int kDocAlignItems = static_cast<int>(std::size(kDocAligns));
constexpr int kDocStringItems = static_cast<int>(std::size(kDocStrings));

uint32_t DocAttributeItem(int which)
{
    Fnv f;
    if (which < kDocBackgroundItems) {
        // BGCOLOR into a TABLE, a TR and a TD.
        const int kind = which % 3;
        const std::uintptr_t ctors[] = {addr::HTMLTableNode_Construct, addr::HTMLRowNode_Construct,
                                        addr::HTMLCellNode_Construct};
        const uint32_t sizes[] = {0x94, 0x3C, 0x3C};
        char* const node = static_cast<char*>(GameNew(sizes[kind]));
        std::memset(node, kPattern, sizes[kind]);
        CallGameThis<char*>(ctors[kind], node);
        GameText text;
        MakeText(&text, kDocColours[which / 3]);
        CallGameThis<void>(kind == 0 ? addr::HTMLTableNode_SetBackground : addr::HTMLNode_SetBackground, node,
                           static_cast<const GameText*>(&text));
        DestroyText(&text);
        const std::size_t at = kind == 0 ? 0x44 : 0x34;
        HashWord(f, At<uint32_t>(node, at));
        f.Byte(At<uint8_t>(node, at + 4));
        Virtual<char*>(node, 0x0, uint32_t{1});
        return f.h;
    }
    which -= kDocBackgroundItems;
    if (which < kDocAlignItems) {
        // Both ALIGN parsers, each on its own copy: the number and the text.
        for (const std::uintptr_t fn : {addr::HTMLText_ParseAlign, addr::HTMLText_ParseImageAlign}) {
            GameText text;
            MakeText(&text, kDocAligns[which]);
            f.Word(reinterpret_cast<int32_t(__cdecl*)(GameText*)>(fn)(&text));
            f.Bytes(text.begin, text.end);
            DestroyText(&text);
        }
        return f.h;
    }
    which -= kDocAlignItems;
    GameText text;
    MakeText(&text, kDocStrings[which]);
    const GameText* const value = &text;
    // A's three strings.
    char* const link = static_cast<char*>(GameNew(0x34));
    std::memset(link, kPattern, 0x34);
    CallGameThis<char*>(addr::HTMLLinkNode_Construct, link);
    CallGameThis<void>(addr::HTMLLinkNode_SetHref, link, value);
    CallGameThis<void>(addr::HTMLLinkNode_SetAddress, link, value);
    CallGameThis<void>(addr::HTMLLinkNode_SetName, link, value);
    for (const uint16_t at : {0x28, 0x2C, 0x30})
        HashField(f, link, {at, Field::TextPtr});
    Virtual<char*>(link, 0x0, uint32_t{1});
    // TD's NOWRAP.
    char* const cell = static_cast<char*>(GameNew(0x3C));
    std::memset(cell, kPattern, 0x3C);
    CallGameThis<char*>(addr::HTMLCellNode_Construct, cell);
    CallGameThis<void>(addr::HTMLCellNode_SetNoWrap, cell, value);
    f.Byte(At<uint8_t>(cell, 0x39));
    f.Bytes(text.begin, text.end);
    Virtual<char*>(cell, 0x0, uint32_t{1});
    // MAP's NAME (IMG's ALT is the same function).
    char* const map = static_cast<char*>(GameNew(0x2C));
    std::memset(map, kPattern, 0x2C);
    CallGameThis<char*>(addr::HTMLMapNode_Construct, map);
    CallGameThis<void>(addr::HTMLMapNode_SetName, map, value);
    HashField(f, map, {0x28, Field::TextPtr});
    Virtual<char*>(map, 0x0, uint32_t{1});
    // META's three.
    char* const meta = static_cast<char*>(GameNew(0x28));
    std::memset(meta, kPattern, 0x28);
    CallGameThis<char*>(addr::HTMLMetaNode_Construct, meta);
    CallGameThis<void>(addr::HTMLMetaNode_SetName, meta, value);
    CallGameThis<void>(addr::HTMLMetaNode_SetContent, meta, value);
    CallGameThis<void>(addr::HTMLMetaNode_SetHttpEquiv, meta, value);
    for (const uint16_t at : {0x1C, 0x20, 0x24})
        HashField(f, meta, {at, Field::TextPtr});
    Virtual<char*>(meta, 0x0, uint32_t{1});
    DestroyText(&text);
    return f.h;
}

constexpr int kDocAttributeItems = kDocBackgroundItems + kDocAlignItems + kDocStringItems;

// The tokenizer's helpers: the hash code, trimming and the tag's name split
// on strings; the tag table's find and bucket on names; the attribute map
// of tags read by NextTag, its values taken and left, cleared; spaces
// skipped at every position of a page.
const char* const kDocTokenStrings[] = {"",          "IMG SRC=a",   "img",        "  p  ",      "a\tb",
                                        "td  align=left  ", "\xC3\xA9 x", "br/",  " x",         "x ",
                                        "\xFF\x80 \xFF", "A\r\nB",   "  ",         "\xE2\x80\x8B x", "TABLE  "};
const char* const kDocTagNames[] = {"IMG", "img", "TABLE", "TD", "/TD", "NOBR", "!DOCTYPE", "BLINK", "NOSUCH", "",
                                    "A",   "B",   "ba",    "HTML", "/HTML", "\xC3\xA9"};
const char* const kDocTags[] = {
    "<IMG SRC=\"a\" ALT='b c' WIDTH=3 AB=1 BA=2 CA=3 AC=4 X Y=\" y \" Z='' src=low>",
    "<TD ALIGN=left NOWRAP BGCOLOR=\"#fff\" VALIGN=top WIDTH=\"5%\" ab ba aab aba baa>",
    "<P>",
    "<A HREF=x HREF=y NAME=n>",
};
const char* const kDocAttributeNames[] = {"SRC", "ALT", "AB", "BA", "CA", "AC", "X", "Y", "Z", "NONE", "src",
                                          "ALIGN", "NOWRAP", "AAB", "ABA", "BAA", "HREF", "NAME", ""};

constexpr int kDocStringTokenItems = static_cast<int>(std::size(kDocTokenStrings));
constexpr int kDocTagItems = static_cast<int>(std::size(kDocTagNames));
constexpr int kDocAttributeMapItems = static_cast<int>(std::size(kDocTags)) * 2;

uint32_t DocTokenItem(int which)
{
    Fnv f;
    if (which < kDocStringTokenItems) {
        GameText text;
        MakeText(&text, kDocTokenStrings[which]);
        f.Word(CallGameThis<uint32_t>(addr::HTMLStringHash_Hash, nullptr, static_cast<const GameText*>(&text)));
        reinterpret_cast<void(__cdecl*)(GameText*)>(addr::HTMLText_Trim)(&text);
        f.Bytes(text.begin, text.end);
        DestroyText(&text);
        GameText tag;
        GameText rest;
        MakeText(&tag, kDocTokenStrings[which]);
        MakeText(&rest, "left over");
        reinterpret_cast<void(__cdecl*)(GameText*, GameText*)>(addr::HTMLDocument_SplitTagName)(&tag, &rest);
        f.Bytes(tag.begin, tag.end);
        f.Bytes(rest.begin, rest.end);
        DestroyText(&tag);
        DestroyText(&rest);
        return f.h;
    }
    which -= kDocStringTokenItems;
    if (which < kDocTagItems) {
        GameText name;
        MakeText(&name, kDocTagNames[which]);
        char* const table = reinterpret_cast<char*>(addr::HTMLTags_Table);
        f.Word(CallGameThis<uint32_t>(addr::HTMLStringHash_Bucket, table, static_cast<const GameText*>(&name)));
        char* const node =
            CallGameThis<char*>(addr::HTMLTagTable_Find, table, static_cast<const GameText*>(&name));
        f.Word(node ? At<uint32_t>(node, 0x18) : 0xFFFFFFFFu);
        DestroyText(&name);
        return f.h;
    }
    which -= kDocTagItems;
    using NewFn = void*(__cdecl*)(std::size_t);
    using ThisFn = void*(__thiscall*)(void*);
    char* const doc = static_cast<char*>(reinterpret_cast<NewFn>(addr::SC4_operator_new)(0x1C4));
    reinterpret_cast<ThisFn>(addr::HTMLDocument_Construct)(doc);
    Virtual<uint32_t>(doc, 0x4);
    GameText text;
    if (which < kDocAttributeMapItems) {
        // A tag's attributes, each asked for (taken or left by variant),
        // the map hashed after each, then checked and cleared.
        MakeText(&text, kDocTags[which / 2]);
        reinterpret_cast<void(__thiscall*)(void*, const GameText*)>(addr::cRZBaseString_Assign)(doc + 0x180, &text);
        DestroyText(&text);
        At<uint32_t>(doc, 0x194) = 0;
        reinterpret_cast<void(__thiscall*)(void*, uint8_t)>(addr::HTMLDocument_NextTag)(doc, 0);
        HashAttributes(f, doc);
        int n = 0;
        for (const char* name : kDocAttributeNames) {
            GameText value;
            MakeText(&value, "was");
            const uint8_t take = static_cast<uint8_t>((which % 2) ? (n++ % 2) : 1);
            f.Byte(CallGameThis<uint8_t>(addr::HTMLDocument_GetAttribute, doc, name, &value, take) & 1);
            f.Bytes(value.begin, value.end);
            DestroyText(&value);
            HashAttributes(f, doc);
        }
        CallGameThis<void>(addr::HTMLDocument_CheckAttributes, doc, "handler", "tag");
        // An iterator at no node, erased: nothing happens.
        CallGameThis<void>(addr::HTMLAttributes_Erase, doc + 0x114, static_cast<char*>(nullptr), doc + 0x114);
        CallGameThis<void>(addr::HTMLAttributes_Clear, doc + 0x114);
        HashAttributes(f, doc);
    } else {
        // Spaces skipped from every position.
        MakeText(&text, "  \t\r\n x\f\vy \xC3\xA9  \xE2\x80\x8B z ");
        reinterpret_cast<void(__thiscall*)(void*, const GameText*)>(addr::cRZBaseString_Assign)(doc + 0x180, &text);
        const uint32_t n = static_cast<uint32_t>(text.end - text.begin);
        DestroyText(&text);
        for (uint32_t i = 0; i <= n; ++i) {
            At<uint32_t>(doc, 0x194) = i;
            CallGameThis<void>(addr::HTMLDocument_SkipSpaces, doc);
            f.Word(At<uint32_t>(doc, 0x194));
        }
    }
    Virtual<uint32_t>(doc, 0x8);
    return f.h;
}

constexpr int kDocTokenItems = kDocStringTokenItems + kDocTagItems + kDocAttributeMapItems + 1;

// The small handlers, as pages: each parsed as the parser test parses it,
// and the document's base, LINK's and META's nodes and IMG's ALT hashed
// with the tree.
const char* const kDocPages[] = {
    // BR and HR.
    "<HTML><BODY>a<BR>b<BR CLEAR=all><HR><HR ALIGN=left SIZE=3 WIDTH=50%><HR ALIGN=RIGHT WIDTH=200>"
    "<HR align=center size=0 width=\"%\"><HR ALIGN=top SIZE=x WIDTH=12%3><HR ALIGN=Middle><HR ALIGN=bottom>"
    "<HR ALIGN=nowhere SIZE=\"\" WIDTH=\"\"><P>p<BR></P></BODY></HTML>",
    // INPUT, MAP's AREAs.
    "<HTML><BODY><FORM><INPUT TYPE=text NAME=a VALUE=b><INPUT></FORM>x<INPUT CHECKED>"
    "<MAP NAME=m><AREA SHAPE=rect COORDS=\"0,0,1,1\" HREF=a><AREA SHAPE=CIRCLE><AREA SHAPE=poly><AREA SHAPE=default>"
    "<AREA SHAPE=oval><AREA><AREA SHAPE=\"Rect\" NOHREF></MAP><IMG SRC=a.png USEMAP=#m></BODY></HTML>",
    // IMG in every form, and in aligned blocks.
    "<HTML><BODY><IMG SRC=\"a.png\" ALT=\"an image\" WIDTH=10 HEIGHT=20 ALIGN=left><IMG SRC=b.gif ALIGN=RIGHT>"
    "<IMG SRC=\"\" ALT=><IMG ALT=nosrc WIDTH=3><IMG SRC=c.jpg ALIGN=top WIDTH=x HEIGHT=-5><IMG SRC=d ALIGN=middle>"
    "<IMG SRC=e ALIGN=bottom><IMG SRC=f ALIGN=center><CENTER><IMG SRC=g></CENTER><DIV ALIGN=right><IMG SRC=h>"
    "</DIV><A HREF=x><IMG SRC=\"sub/i.png\" ALT=\"\xC3\xA9\"></A></BODY></HTML>",
    // The head with a base.
    "<HTML><HEAD><BASE HREF=\"http://example.com/dir/\"><LINK REL=stylesheet HREF=\"s.css\" TITLE=t>"
    "<LINK REV=made HREF=\"mailto:x\"><LINK TITLE=only><META NAME=a CONTENT=b><META HTTP-EQUIV=refresh CONTENT=\"5\">"
    "<META NAME=nocontent><META></HEAD><BODY><IMG SRC=\"rel.png\"><A HREF=\"r.html\">r</A></BODY></HTML>",
    // A base with no HREF, LINK and META with odd values.
    "<HTML><HEAD><BASE><BASE TARGET=x><LINK HREF=a.css><LINK HREF=\"\" REV=\"\" TITLE=\"\">"
    "<META CONTENT=c><META CONTENT=\"\" NAME=\"\"></HEAD><BODY><IMG SRC=x.png></BODY></HTML>",
    // Tables' and cells' attributes.
    "<HTML><BODY><TABLE BGCOLOR=red><TR BGCOLOR=\"#00ff00\" ALIGN=center><TD BGCOLOR=blue ALIGN=RIGHT NOWRAP>a</TD>"
    "<TD NOWRAP=nowrap ALIGN=left>b</TD><TD ALIGN=CENTER VALIGN=top>c</TD><TD ALIGN=justify BGCOLOR=\"\">d</TD></TR>"
    "<TR BGCOLOR=nothing><TD>e</TD></TR></TABLE><A HREF=\"h.html\" NAME=\"n\">l</A><A NAME=only>m</A></BODY></HTML>",
};

void HashDocLists(Fnv& f, char* doc)
{
    HashField(f, doc, {0xB8, Field::TextPtr});
    char** const metas = At<char**>(doc, 0xAC);
    for (char** at = metas; at && at < At<char**>(doc, 0xB0); ++at) {
        f.Byte(0xC1);
        for (const uint16_t field : {0x1C, 0x20, 0x24})
            HashField(f, *at, {field, Field::TextPtr});
    }
    char** const links = At<char**>(doc, 0xBC);
    for (char** at = links; at && at < At<char**>(doc, 0xC0); ++at) {
        f.Byte(0xC2);
        HashField(f, *at, {0x1C, Field::TextPtr});
        f.Byte(At<uint8_t>(*at, 0x20));
        HashField(f, *at, {0x24, Field::TextPtr});
        HashField(f, *at, {0x28, Field::TextPtr});
    }
}

// Every IMG's ALT (+0x28) in the tree.
void HashAlts(Fnv& f, char* node, int depth)
{
    if (!node || depth > 64)
        return;
    const uint32_t vtable = At<uint32_t>(node, 0);
    if (vtable == 0x00ACE188) {
        f.Byte(0xC3);
        HashField(f, node, {0x28, Field::TextPtr});
        return;
    }
    const NodeClass* const c = ClassOf(vtable);
    if (!c || !c->container)
        return;
    for (char** at = At<char**>(node, 0x1C); at && at < At<char**>(node, 0x20); ++at)
        HashAlts(f, *at, depth + 1);
}

uint32_t DocPageItem(char* window, int which)
{
    using NewFn = void*(__cdecl*)(std::size_t);
    using ThisFn = void*(__thiscall*)(void*);
    using InitFn = uint8_t(__thiscall*)(void*, void*, void*, const GameText*);
    char* const doc = static_cast<char*>(reinterpret_cast<NewFn>(addr::SC4_operator_new)(0x1C4));
    reinterpret_cast<ThisFn>(addr::HTMLDocument_Construct)(doc);
    Virtual<uint32_t>(doc, 0x4);
    GameText base;
    MakeText(&base, which % 2 ? "http://maxis.com/a/b.html" : "");
    reinterpret_cast<InitFn>(addr::HTMLDocument_Init)(doc, window, &g_service, &base);
    DestroyText(&base);
    GameText text;
    MakeText(&text, kDocPages[which / 2]);
    const bool parsed = Virtual<uint8_t>(doc, 0x20, &text) != 0;
    DestroyText(&text);
    Fnv f;
    f.Byte(parsed ? 1 : 0);
    HashDocument(f, doc);
    HashDocLists(f, doc);
    HashAlts(f, At<char*>(doc, 0x14), 0);
    Virtual<uint32_t>(doc, 0x8);
    return f.h;
}

constexpr int kDocPageItems = static_cast<int>(std::size(kDocPages)) * 2;

// h2b1's: the document's slots, its Init, base and destructors, the parser's
// glue (text nodes, the flow, the tag sets, the alignment stack's push) and
// the font stack.

// A document as ShowHTML makes one, with a reference, Init (the window, the
// stand-in service, the base) and a page with anchors, fonts and an aligned
// block parsed.
const char* const kSlotPage =
    "<HTML><BODY><A NAME=top>t</A><A NAME=\"Second\">s</A><A HREF=x NAME=third>3</A><A HREF=y>n</A>"
    "<FONT SIZE=2 FACE=\"times\"><B>bold<I>it</I></B></FONT><CENTER>c<DIV ALIGN=right>r</DIV></CENTER></BODY></HTML>";

char* NewDocument()
{
    using NewFn = void*(__cdecl*)(std::size_t);
    using ThisFn = void*(__thiscall*)(void*);
    char* const doc = static_cast<char*>(reinterpret_cast<NewFn>(addr::SC4_operator_new)(0x1C4));
    reinterpret_cast<ThisFn>(addr::HTMLDocument_Construct)(doc);
    Virtual<uint32_t>(doc, 0x4);
    return doc;
}

char* MakeSlotDocument(char* window, const char* base, const char* page = kSlotPage)
{
    using InitFn = uint8_t(__thiscall*)(void*, void*, void*, const GameText*);
    char* const doc = NewDocument();
    GameText text;
    MakeText(&text, base);
    reinterpret_cast<InitFn>(addr::HTMLDocument_Init)(doc, window, &g_service, &text);
    DestroyText(&text);
    if (page) {
        MakeText(&text, page);
        Virtual<uint8_t>(doc, 0x20, &text);
        DestroyText(&text);
    }
    return doc;
}

// A window of the test's own for the document's slot 17: its slot 92
// counted.
uint32_t g_redraws = 0;
uint32_t __fastcall FakeRedraw(void*, void*)
{
    ++g_redraws;
    return 0x1234;
}
void* g_fakeWindowVtable[96] = {};
struct FakeWindow {
    void** vtable;
} g_fakeWindow{g_fakeWindowVtable};

const uint32_t kQueryIds[] = {1, 0x452294AAu, 0xE0ECA23Au, 0x12345678u, 0};
const int32_t kStepCounts[] = {0, 1, 4, 7, 8, 12};
const int32_t kSetCounts[] = {0, 1, 3, 4, 7, 9};
const char* const kAnchorNames[] = {"top", "TOP", "Second", "third", "none", "", "n"};
const uint8_t kFlags29[] = {0, 1, 0x80};

constexpr int kSlotQueryItems = static_cast<int>(std::size(kQueryIds)) * 2;
constexpr int kSlotItems = kSlotQueryItems + 2 + static_cast<int>(std::size(kStepCounts)) +
                           static_cast<int>(std::size(kSetCounts)) + static_cast<int>(std::size(kAnchorNames)) +
                           static_cast<int>(std::size(kFlags29)) + 1 + 2 + 3 + 2;

uint32_t DocSlotItem(char* window, int which)
{
    Fnv f;
    char* const doc = MakeSlotDocument(window, which % 2 ? "http://maxis.com/a/" : "");
    if (which < kSlotQueryItems) {
        // QueryInterface through the document and its part at +4.
        char* const self = which % 2 ? doc + 4 : doc;
        void* out = reinterpret_cast<void*>(0x77);
        const uint8_t r = Virtual<uint8_t>(self, 0x0, kQueryIds[which / 2], &out);
        f.Byte(r & 1);
        f.Word(out == reinterpret_cast<void*>(0x77) ? 0xEEEEu
                                                    : out ? static_cast<uint32_t>(static_cast<char*>(out) - doc)
                                                          : 0xFFFFFFFFu);
        f.Word(At<uint32_t>(doc, 0x1C));
        if (r & 1)
            Virtual<uint32_t>(doc, 0x8);
        Virtual<uint32_t>(doc, 0x8);
        return f.h;
    }
    which -= kSlotQueryItems;
    if (which == 0) {
        // AddRef and Release through both parts; the last release deletes.
        f.Word(Virtual<uint32_t>(doc, 0x4));
        f.Word(Virtual<uint32_t>(doc + 4, 0x4));
        f.Word(Virtual<uint32_t>(doc + 4, 0x8));
        f.Word(Virtual<uint32_t>(doc, 0x8));
        f.Word(Virtual<uint32_t>(doc + 4, 0x8));
        return f.h;
    }
    if (which == 1) {
        f.Word(static_cast<uint32_t>(Virtual<char*>(doc, 0x10) - doc));
        Virtual<uint32_t>(doc, 0x8);
        return f.h;
    }
    which -= 2;
    if (which < static_cast<int>(std::size(kStepCounts))) {
        int32_t out[12];
        for (int32_t& v : out)
            v = 0x5A5A5A5A;
        f.Word(Virtual<uint32_t>(doc, 0x18, out, kStepCounts[which]));
        for (const int32_t v : out)
            f.Word(static_cast<uint32_t>(v));
        Virtual<uint32_t>(doc, 0x8);
        return f.h;
    }
    which -= static_cast<int>(std::size(kStepCounts));
    if (which < static_cast<int>(std::size(kSetCounts))) {
        const int32_t steps[] = {5, 6, 7, 8, 9, 10, 11, 12, 13};
        f.Word(Virtual<uint32_t>(doc, 0x1C, steps, kSetCounts[which]));
        for (std::size_t at = 0x1A8; at < 0x1C4; at += 4)
            f.Word(At<uint32_t>(doc, at));
        f.Word(At<uint32_t>(doc, 0x54));
        f.Word(At<uint32_t>(doc, 0x6C));
        Virtual<uint32_t>(doc, 0x8);
        return f.h;
    }
    which -= static_cast<int>(std::size(kSetCounts));
    if (which < static_cast<int>(std::size(kAnchorNames))) {
        GameText name;
        MakeText(&name, kAnchorNames[which]);
        char* const node = Virtual<char*>(doc, 0x34, &name);
        DestroyText(&name);
        int index = -1;
        char** const first = At<char**>(doc, 0xC8);
        for (char** at = first; at && at < At<char**>(doc, 0xCC); ++at)
            if (*at == node)
                index = static_cast<int>(at - first);
        f.Byte(node != nullptr);
        f.Word(static_cast<uint32_t>(index));
        f.Word(static_cast<uint32_t>(At<char**>(doc, 0xCC) - first));
        Virtual<uint32_t>(doc, 0x8);
        return f.h;
    }
    which -= static_cast<int>(std::size(kAnchorNames));
    if (which < static_cast<int>(std::size(kFlags29))) {
        Virtual<void>(doc, 0x3C, kFlags29[which]);
        f.Byte(At<uint8_t>(doc, 0x29));
        f.Byte(At<uint8_t>(doc, 0x2A));
        Virtual<uint32_t>(doc, 0x8);
        return f.h;
    }
    which -= static_cast<int>(std::size(kFlags29));
    if (which == 0) {
        f.Word(Virtual<uint32_t>(doc, 0x48));
        f.Word(Virtual<uint32_t>(doc, 0x4C));
        f.Byte(Virtual<uint8_t>(doc, 0x50));
        Virtual<uint32_t>(doc, 0x8);
        return f.h;
    }
    which -= 1;
    if (which < 2) {
        // Slot 17 with a window of the test's own, and with none.
        g_fakeWindowVtable[92] = reinterpret_cast<void*>(&FakeRedraw);
        char* const kept = At<char*>(doc, 0x2C);
        At<FakeWindow*>(doc, 0x2C) = which == 0 ? &g_fakeWindow : nullptr;
        g_redraws = 0;
        const uint32_t r = Virtual<uint32_t>(doc, 0x44);
        f.Word(which == 0 ? r : 0);
        f.Word(g_redraws);
        At<char*>(doc, 0x2C) = kept;
        Virtual<uint32_t>(doc, 0x8);
        return f.h;
    }
    which -= 2;
    if (which < 3) {
        // The base: the two documents' and none.
        char* const kept = At<char*>(doc, 0xB8);
        if (which == 2)
            At<char*>(doc, 0xB8) = nullptr;
        GameText out;
        MakeText(&out, "was");
        f.Byte(Virtual<uint8_t>(doc, 0x54, &out) & 1);
        f.Bytes(out.begin, out.end);
        DestroyText(&out);
        At<char*>(doc, 0xB8) = kept;
        Virtual<uint32_t>(doc, 0x8);
        return f.h;
    }
    which -= 3;
    // The two faces.
    GameText out;
    MakeText(&out, "was");
    uint32_t size = 0x99;
    f.Word(Virtual<uint32_t>(doc, which == 0 ? 0x58 : 0x5C, &out, &size));
    f.Bytes(out.begin, out.end);
    f.Word(size);
    DestroyText(&out);
    Virtual<uint32_t>(doc, 0x8);
    return f.h;
}

// Init on new documents (two bases, the service or none), then again; the
// base set from none and over one; the destructors.
constexpr int kLifeItems = 4 + 2 + 3;

// Whether each block was freed, asking the allocator for up to 256 blocks of
// its size (a small block freed early in a destructor sits under the ones
// freed after it in the allocator's free list); the blocks handed out are
// freed again, the last first.
void HashFreedDeep(Fnv& f, const Blocks& blocks)
{
    for (std::size_t i = blocks.blocks.size(); i-- > 0;) {
        std::vector<void*> again;
        bool freed = false;
        for (int n = 0; n < 256 && !freed; ++n) {
            void* const p = blocks.viaNew[i] ? GameNew(blocks.sizes[i]) : GameAllocate(blocks.sizes[i]);
            again.push_back(p);
            freed = p == blocks.blocks[i];
        }
        f.Byte(freed ? 1 : 0);
        ++g_blocksChecked;
        g_blocksFreed += freed ? 1 : 0;
        for (std::size_t j = again.size(); j-- > 0;) {
            if (blocks.viaNew[i])
                GameDelete(again[j]);
            else
                GameFree(again[j]);
        }
    }
}

void HashInit(Fnv& f, char* doc, char* window)
{
    f.Byte(At<uint8_t>(doc, 0x18));
    f.Byte(At<char*>(doc, 0x2C) == window);
    f.Byte(At<void*>(doc, 0x30) == &g_service);
    HashField(f, doc, {0xB8, Field::TextPtr});
    for (std::size_t at = 0x144; at <= 0x150; at += 4)
        f.Word(At<uint32_t>(doc, at));
    f.Byte(At<uint8_t>(doc, 0x154));
    f.Byte(At<uint8_t>(doc, 0x155));
}

uint32_t DocLifeItem(char* window, int which)
{
    using InitFn = uint8_t(__thiscall*)(void*, void*, void*, const GameText*);
    using BaseFn = uint8_t(__thiscall*)(void*, const GameText*);
    Fnv f;
    if (which < 4) {
        char* const doc = NewDocument();
        GameText base;
        MakeText(&base, which % 2 ? "http://example.com/x/y.html" : "");
        f.Byte(reinterpret_cast<InitFn>(addr::HTMLDocument_Init)(doc, window, which < 2 ? &g_service : nullptr,
                                                                  &base) &
               1);
        DestroyText(&base);
        HashInit(f, doc, window);
        MakeText(&base, "other");
        f.Byte(reinterpret_cast<InitFn>(addr::HTMLDocument_Init)(doc, window, &g_service, &base) & 1);
        DestroyText(&base);
        HashInit(f, doc, window);
        Virtual<uint32_t>(doc, 0x8);
        return f.h;
    }
    which -= 4;
    if (which < 2) {
        // The base from none (a new string), then assigned; or assigned only.
        char* const doc = MakeSlotDocument(window, "start", nullptr);
        char* const kept = At<char*>(doc, 0xB8);
        if (which == 0)
            At<char*>(doc, 0xB8) = nullptr;
        for (const char* text : {"a/b.html", "", "http://e.com/"}) {
            GameText base;
            MakeText(&base, text);
            f.Byte(reinterpret_cast<BaseFn>(addr::HTMLDocument_SetBase)(doc, &base) & 1);
            DestroyText(&base);
            HashField(f, doc, {0xB8, Field::TextPtr});
        }
        if (which == 0) {
            GameText* const made = At<GameText*>(doc, 0xB8);
            DestroyText(made);
            GameDelete(made);
            At<char*>(doc, 0xB8) = kept;
        }
        Virtual<uint32_t>(doc, 0x8);
        return f.h;
    }
    which -= 2;
    // The destructors: the deleting one through Release, with the
    // document's block and its vectors' storage checked for being freed; the
    // destructor alone and the deleting one with flag 0, the document hashed
    // after.
    char* const doc = MakeSlotDocument(window, "http://maxis.com/");
    Blocks blocks;
    for (const std::size_t at : {0xACu, 0xBCu, 0xC8u, 0x15Cu, 0x168u, 0x174u}) {
        char* const first = At<char*>(doc, at);
        if (first)
            blocks.Track(first, static_cast<std::size_t>(At<char*>(doc, at + 8) - first), false);
    }
    f.Word(static_cast<uint32_t>(blocks.blocks.size()));
    if (which == 0) {
        blocks.Track(doc, 0x1C4, true);
        f.Word(Virtual<uint32_t>(doc, 0x8));
    } else {
        if (which == 1)
            CallGameThis<void>(addr::HTMLDocument_Destruct, doc);
        else
            f.Byte(CallGameThis<char*>(addr::HTMLDocument_Delete, doc, uint32_t{0}) == doc ? 1 : 0);
        // The vtables put back (the document's and its strings'); the rest
        // is freed pointers and the lock's handles.
        for (const std::size_t at : {0x0u, 0x4u, 0x40u, 0x58u, 0x70u, 0x84u, 0x98u, 0xD4u, 0xECu, 0x100u, 0x130u,
                                     0x180u})
            f.Word(At<uint32_t>(doc, at));
        f.Byte(At<uint8_t>(doc, 0x18));
        f.Word(At<uint32_t>(doc, 0x1C));
    }
    HashFreedDeep(f, blocks);
    if (which != 0)
        GameDelete(doc);
    return f.h;
}

// The tag sets: each one's membership of every ID; the flow with IDs in no
// set and with text; text nodes; the sets filled again into new sets; the
// alignment stack's push.
const uint32_t kTagSets[] = {0x00B624B8, 0x00B624C4, 0x00B624D0, 0x00B624DC, 0x00B624E8,
                             0x00B624F4, 0x00B62500, 0x00B6250C, 0x00B62518};
const char* const kAddTexts[] = {"plain words", "a &amp; b &lt;c&gt;", ""};

constexpr int kGlueItems = static_cast<int>(std::size(kTagSets)) + 1 + static_cast<int>(std::size(kAddTexts)) + 1 + 2;

void HashTree(Fnv& f, char* node, int depth)
{
    if (!node || depth > 64) {
        f.Byte(0xD0);
        return;
    }
    f.Byte(At<uint8_t>(node, 0x0));
    f.Word(At<uint32_t>(node, 0x10));
    HashTree(f, At<char*>(node, 0x8), depth + 1);
    HashTree(f, At<char*>(node, 0xC), depth + 1);
}

void FreeTree(char* node, int depth)
{
    if (!node || depth > 64)
        return;
    FreeTree(At<char*>(node, 0x8), depth + 1);
    FreeTree(At<char*>(node, 0xC), depth + 1);
    GameFree(node);
}

char* NewParagraph()
{
    char* const node = static_cast<char*>(GameNew(0x28));
    CallGameThis<char*>(addr::HTMLParagraphNode_Construct, node);
    return node;
}

uint32_t DocGlueItem(char* window, int which)
{
    Fnv f;
    if (which < static_cast<int>(std::size(kTagSets))) {
        char* const set = reinterpret_cast<char*>(static_cast<std::uintptr_t>(kTagSets[which]));
        for (uint32_t id = 0; id < 0xA4; ++id)
            f.Byte(reinterpret_cast<uint8_t(__cdecl*)(uint32_t, char*)>(addr::HTMLTagSet_Contains)(id, set) & 1);
        return f.h;
    }
    which -= static_cast<int>(std::size(kTagSets));
    if (which == 0) {
        char* const doc = MakeSlotDocument(window, "");
        char* const parent = NewParagraph();
        for (const uint32_t id : {0u, 0xA2u, 0x100u, 3u}) {
            At<uint32_t>(doc, 0xE8) = id;
            f.Byte(CallGameThis<uint8_t>(addr::HTMLDocument_ParseFlow, doc, parent) & 1);
            f.Word(At<uint32_t>(doc, 0xE8));
        }
        HashNode(f, parent, 0);
        Virtual<char*>(parent, 0x0, uint32_t{1});
        Virtual<uint32_t>(doc, 0x8);
        return f.h;
    }
    which -= 1;
    if (which < static_cast<int>(std::size(kAddTexts))) {
        char* const doc = MakeSlotDocument(window, "", nullptr);
        char* const parent = NewParagraph();
        GameText text;
        MakeText(&text, kAddTexts[which]);
        CallGameThis<void>(addr::HTMLDocument_AddText, doc, static_cast<const GameText*>(&text), parent);
        CallGameThis<void>(addr::HTMLDocument_AddText, doc, static_cast<const GameText*>(&text), parent);
        DestroyText(&text);
        HashNode(f, parent, 0);
        Virtual<char*>(parent, 0x0, uint32_t{1});
        Virtual<uint32_t>(doc, 0x8);
        return f.h;
    }
    which -= static_cast<int>(std::size(kAddTexts));
    if (which == 0) {
        // The sets filled again into new, empty sets (the game's put back
        // after): each one's count and tree.
        uint8_t saved[std::size(kTagSets)][12];
        for (std::size_t i = 0; i < std::size(kTagSets); ++i) {
            char* const set = reinterpret_cast<char*>(static_cast<std::uintptr_t>(kTagSets[i]));
            std::memcpy(saved[i], set, 12);
            char* const header = static_cast<char*>(GameAllocate(0x14));
            At<uint32_t>(header, 0x0) = At<uint32_t>(At<char*>(set, 0x0), 0x0);
            At<char*>(header, 0x4) = nullptr;
            At<char*>(header, 0x8) = header;
            At<char*>(header, 0xC) = header;
            At<char*>(set, 0x0) = header;
            At<uint32_t>(set, 0x4) = 0;
        }
        reinterpret_cast<void(__cdecl*)()>(addr::HTMLTagSets_Init)();
        for (std::size_t i = 0; i < std::size(kTagSets); ++i) {
            char* const set = reinterpret_cast<char*>(static_cast<std::uintptr_t>(kTagSets[i]));
            char* const header = At<char*>(set, 0x0);
            f.Word(At<uint32_t>(set, 0x4));
            f.Word(At<char*>(header, 0x8) ? At<uint32_t>(At<char*>(header, 0x8), 0x10) : 0xFFFFFFFFu);
            f.Word(At<char*>(header, 0xC) ? At<uint32_t>(At<char*>(header, 0xC), 0x10) : 0xFFFFFFFFu);
            HashTree(f, At<char*>(header, 0x4), 0);
            FreeTree(At<char*>(header, 0x4), 0);
            GameFree(header);
            std::memcpy(set, saved[i], 12);
        }
        return f.h;
    }
    which -= 1;
    // The alignment stack's push: from no storage, and with room for four.
    char* vector[3] = {};
    if (which == 1) {
        vector[0] = static_cast<char*>(GameAllocate(16));
        vector[1] = vector[0];
        vector[2] = vector[0] + 16;
    }
    for (int32_t value = 1; value <= 9; ++value) {
        CallGameThis<void>(addr::HTMLIntVector_PushBack, vector, static_cast<const int32_t*>(&value));
        f.Word(static_cast<uint32_t>(vector[1] - vector[0]));
        f.Word(static_cast<uint32_t>(vector[2] - vector[0]));
        for (const int32_t* at = reinterpret_cast<int32_t*>(vector[0]); at < reinterpret_cast<int32_t*>(vector[1]);
             ++at)
            f.Word(static_cast<uint32_t>(*at));
    }
    if (vector[0])
        GameFree(vector[0]);
    return f.h;
}

// The font stack: a record's copy and assignment; ranges copied, filled and
// destroyed; a stack of the test's own pushed, grown and destroyed; the
// document's pop.
struct GameFont {
    GameText face;
    uint32_t words[4];
    uint8_t flags[2];
    uint8_t spare[2];
};
static_assert(sizeof(GameFont) == 0x28);

const char* const kFaces[] = {"Arial, Helvetica", "courier new", "", "times", "\xC3\xA9 face"};

void MakeFont(GameFont* font, int i)
{
    MakeText(&font->face, kFaces[i % std::size(kFaces)]);
    for (int w = 0; w < 4; ++w)
        font->words[w] = 0x100u * static_cast<uint32_t>(i + 1) + static_cast<uint32_t>(w);
    font->flags[0] = static_cast<uint8_t>(i & 1);
    font->flags[1] = static_cast<uint8_t>(i * 3);
    font->spare[0] = 0x11;
    font->spare[1] = 0x22;
}

void HashFont(Fnv& f, const GameFont* font)
{
    f.Bytes(font->face.begin, font->face.end);
    for (const uint32_t w : font->words)
        f.Word(w);
    f.Byte(font->flags[0]);
    f.Byte(font->flags[1]);
}

void HashFontStack(Fnv& f, char** stack)
{
    const GameFont* const first = reinterpret_cast<GameFont*>(stack[0]);
    const GameFont* const last = reinterpret_cast<GameFont*>(stack[1]);
    f.Word(static_cast<uint32_t>(last - first));
    f.Word(static_cast<uint32_t>(reinterpret_cast<GameFont*>(stack[2]) - first));
    for (const GameFont* at = first; at < last; ++at)
        HashFont(f, at);
}

constexpr int kFontItems = 2 + 3 + 4 + 1 + 3 + 1;

uint32_t DocFontItem(char* window, int which)
{
    using RangeFn = GameFont*(__cdecl*)(GameFont*, GameFont*, GameFont*, void*);
    using FillFn = GameFont*(__cdecl*)(GameFont*, uint32_t, const GameFont*, void*);
    using DestroyFn = void(__cdecl*)(GameFont*, GameFont*);
    Fnv f;
    uint8_t tag = 0;
    if (which < 2) {
        GameFont from;
        MakeFont(&from, 0);
        GameFont to;
        if (which == 0) {
            std::memset(&to, kPattern, sizeof to);
            f.Byte(CallGameThis<GameFont*>(addr::HTMLFont_Copy, &to, static_cast<const GameFont*>(&from)) == &to);
        } else {
            MakeFont(&to, 1);
            f.Byte(CallGameThis<GameFont*>(addr::HTMLFont_Assign, &to, static_cast<const GameFont*>(&from)) == &to);
        }
        HashFont(f, &to);
        DestroyText(&to.face);
        DestroyText(&from.face);
        return f.h;
    }
    which -= 2;
    GameFont from[3];
    for (int i = 0; i < 3; ++i)
        MakeFont(&from[i], i + 2);
    if (which < 3) {
        // Copied into a patterned range, one into none (only the first copy
        // is skipped for a null `to`: the next would go to 0x28), and an
        // empty range.
        GameFont to[3];
        std::memset(to, kPattern, sizeof to);
        GameFont* const end = reinterpret_cast<RangeFn>(addr::HTMLFont_CopyRange)(
            from, which == 2 ? from : which == 1 ? from + 1 : from + 3, which == 1 ? nullptr : to, &tag);
        f.Word(which == 1 ? static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(end))
                          : static_cast<uint32_t>(end - to));
        if (which == 0)
            for (GameFont& font : to) {
                HashFont(f, &font);
                DestroyText(&font.face);
            }
        for (GameFont& font : from)
            DestroyText(&font.face);
        return f.h;
    }
    which -= 3;
    if (which < 4) {
        // 0, 1 and 4 copies into a patterned range, and 1 into none.
        const uint32_t counts[] = {0, 1, 4, 1};
        GameFont to[4];
        std::memset(to, kPattern, sizeof to);
        GameFont* const end =
            reinterpret_cast<FillFn>(addr::HTMLFont_FillN)(which == 3 ? nullptr : to, counts[which], &from[1], &tag);
        f.Word(which == 3 ? static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(end))
                          : static_cast<uint32_t>(end - to));
        if (which != 3)
            for (uint32_t i = 0; i < counts[which]; ++i) {
                HashFont(f, &to[i]);
                DestroyText(&to[i].face);
            }
        for (GameFont& font : from)
            DestroyText(&font.face);
        return f.h;
    }
    which -= 4;
    if (which == 0) {
        reinterpret_cast<DestroyFn>(addr::HTMLFont_DestroyRange)(from, from + 3);
        HashObject(f, reinterpret_cast<const char*>(from), sizeof from, nullptr);
        return f.h;
    }
    which -= 1;
    if (which < 3) {
        // A stack of the test's own: six pushed (grown on the way), then
        // grown by hand (three in the middle, two at the end, one at the
        // front, by variant), then destroyed with its storage checked.
        char* stack[3] = {};
        for (int i = 0; i < 6; ++i) {
            GameFont font;
            MakeFont(&font, i);
            CallGameThis<void>(addr::HTMLFontStack_Push, stack, static_cast<const GameFont*>(&font));
            DestroyText(&font.face);
            HashFontStack(f, stack);
        }
        GameFont* const first = reinterpret_cast<GameFont*>(stack[0]);
        GameFont* const at = which == 0 ? first + 2 : which == 1 ? reinterpret_cast<GameFont*>(stack[1]) : first;
        const uint32_t count = which == 0 ? 3 : which == 1 ? 2 : 1;
        CallGameThis<void>(addr::HTMLFontStack_Grow, stack, at, static_cast<const GameFont*>(&from[0]),
                           static_cast<void*>(&tag), count, static_cast<uint32_t>(which == 1 ? 1 : 0));
        HashFontStack(f, stack);
        Blocks blocks;
        blocks.Track(stack[0], static_cast<std::size_t>(stack[2] - stack[0]), false);
        CallGameThis<void>(addr::HTMLFontStack_Destroy, stack);
        blocks.HashFreed(f);
        for (GameFont& font : from)
            DestroyText(&font.face);
        return f.h;
    }
    // The document's pop: three pushed, then five popped.
    for (GameFont& font : from)
        DestroyText(&font.face);
    char* const doc = MakeSlotDocument(window, "", nullptr);
    for (int i = 0; i < 3; ++i) {
        GameFont font;
        MakeFont(&font, i + 1);
        CallGameThis<void>(addr::HTMLFontStack_Push, doc + 0x15C, static_cast<const GameFont*>(&font));
        DestroyText(&font.face);
    }
    for (int i = 0; i < 5; ++i) {
        CallGameThis<void>(addr::HTMLDocument_PopFont, doc);
        HashFont(f, reinterpret_cast<const GameFont*>(doc + 0x130));
        HashFontStack(f, reinterpret_cast<char**>(doc + 0x15C));
    }
    Virtual<uint32_t>(doc, 0x8);
    return f.h;
}

// DOCTYPE pages: parsed, the tree and the version strings (+0x70, +0x84)
// hashed.
const char* const kDoctypePages[] = {
    "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.01 Transitional//EN\"><HTML><BODY>a</BODY></HTML>",
    "<!doctype html public \"-//IETF//DTD HTML 2.0//EN\"><html><body>b</body></html>",
    "<!DOCTYPE HTML><HTML><BODY>c</BODY></HTML>",
    "<!DOCTYPE XHTML PUBLIC \"x\"><HTML><BODY>d</BODY></HTML>",
    "<!DOCTYPE HTML PUBLIC \"DTD HTML   3.2.1x//\">e",
    "<!DOCTYPE HTML SYSTEM DTD_HTML_4// \"dtd html 5.0//\"><BODY>f</BODY>",
    "<!DOCTYPE HTM><HTML>g</HTML>",
    "<!DOCTYPE HTML DTD HTML//><P>h",
    "<!DOCTYPE>i",
    "<!DOCTYPE html PUBLIC 'DTD HTML 3.2 Final//EN' x=\"DTD HTML 9//\"><HTML><BODY>j</BODY></HTML>",
};

// Slot 9 on its own: `<!DOCTYPE` leaves the attribute map as the tag before
// left it, and the parser's names stop at a space, so a page never gives it
// a name with `DTD HTML` in it. Here the map holds names of the test's own
// (each node as the map's insert makes one: next, name, value), the tag's
// text after its name is set, and the page after it is a short one.
struct DoctypeCase {
    const char* rest;
    const char* names[2];
};
const DoctypeCase kDoctypeCases[] = {
    {"HTML PUBLIC", {"PUBLIC", "-//W3C//DTD HTML 4.01//EN"}},
    {"html", {"dtd html 2.0//en", nullptr}},
    {"HTML", {"DTD HTML 3.2", "DTD HTML 5//X"}},
    {"HTM", {"DTD HTML 4//EN", nullptr}},
    {"XHTML 1.0", {"DTD HTML 4//EN", nullptr}},
    {"HTML", {"NOTHING", "DTD HTMX"}},
    {"HTML", {"DTD HTML", nullptr}},
    {"HTML SYSTEM", {"DTD HTML    .5.//ab", "x"}},
};

constexpr int kDoctypeItems =
    static_cast<int>(std::size(kDoctypePages)) + static_cast<int>(std::size(kDoctypeCases));

void PutAttribute(char* doc, const char* name)
{
    char* const map = doc + 0x114;
    char* const node = static_cast<char*>(GameAllocate(0x2C));
    MakeText(reinterpret_cast<GameText*>(node + 0x4), name);
    MakeText(reinterpret_cast<GameText*>(node + 0x18), "value");
    const uint32_t bucket =
        CallGameThis<uint32_t>(addr::HTMLStringHash_Bucket, map, reinterpret_cast<const GameText*>(node + 0x4));
    char** const buckets = At<char**>(map, 0x4);
    At<char*>(node, 0x0) = buckets[bucket];
    buckets[bucket] = node;
    ++At<uint32_t>(map, 0x10);
}

uint32_t DoctypeCaseItem(char* window, int which)
{
    const DoctypeCase& c = kDoctypeCases[which];
    char* const doc = MakeSlotDocument(window, "", nullptr);
    Fnv f;
    if (At<char**>(doc, 0x11C) == At<char**>(doc, 0x118)) {
        f.Byte(0xFF);
        Virtual<uint32_t>(doc, 0x8);
        return f.h;
    }
    for (const char* name : c.names)
        if (name)
            PutAttribute(doc, name);
    GameText text;
    MakeText(&text, c.rest);
    reinterpret_cast<void(__thiscall*)(void*, const GameText*)>(addr::cRZBaseString_Assign)(doc + 0xEC, &text);
    DestroyText(&text);
    MakeText(&text, "<HTML><BODY>v</BODY></HTML>");
    reinterpret_cast<void(__thiscall*)(void*, const GameText*)>(addr::cRZBaseString_Assign)(doc + 0x180, &text);
    DestroyText(&text);
    At<uint32_t>(doc, 0x194) = 0;
    f.Byte(Virtual<uint8_t>(doc, 0x24) & 1);
    HashDocument(f, doc);
    Virtual<uint32_t>(doc, 0x8);
    return f.h;
}

uint32_t DocDoctypeItem(char* window, int which)
{
    if (which >= static_cast<int>(std::size(kDoctypePages)))
        return DoctypeCaseItem(window, which - static_cast<int>(std::size(kDoctypePages)));
    char* const doc = MakeSlotDocument(window, "", nullptr);
    GameText text;
    MakeText(&text, kDoctypePages[which]);
    const bool parsed = Virtual<uint8_t>(doc, 0x20, &text) != 0;
    DestroyText(&text);
    Fnv f;
    f.Byte(parsed ? 1 : 0);
    HashDocument(f, doc);
    Virtual<uint32_t>(doc, 0x8);
    return f.h;
}

// h2b2's: the image fetching, the image map, the bullets and slots 5 and
// 20.

// A stand-in's number, 0 for none and 0xEEEE for anything else (a pointer
// of the game's, which changes from run to run).
uint32_t StandNumber(const void* p)
{
    if (!p)
        return 0;
    for (const NodeStand& stand : g_nodeStands)
        if (p == &stand)
            return stand.id;
    return 0xEEEEu;
}

void* StandPtr(int i) { return &g_nodeStands[i]; }

// A picture reader of the test's own (the document's +0x1A4): its slot 4
// reads a file, recording the path (mode 1) or the file's bytes and whether
// it is there (mode 2), then gives the picture `answer` (null leaves the
// picture as it was) and answers `result`. AddRef and Release are recorded.
struct ReaderStand {
    void** vtable;
    uint32_t id;
    int mode;
    void* answer;
    uint32_t result;
};
Fnv g_readerRecord;
std::string g_readerPath;

uint32_t __fastcall ReaderRef(ReaderStand* self, void*)
{
    g_readerRecord.Word(self->id);
    g_readerRecord.Word(1);
    return 2;
}
uint32_t __fastcall ReaderRelease(ReaderStand* self, void*)
{
    g_readerRecord.Word(self->id);
    g_readerRecord.Word(2);
    return 1;
}
uint32_t __fastcall ReaderRead(ReaderStand* self, void*, const GameText* path, void** picture, uint32_t flag)
{
    g_readerRecord.Word(self->id);
    g_readerRecord.Word(4);
    g_readerRecord.Word(flag);
    g_readerRecord.Word(StandNumber(*picture));
    if (self->mode == 1) {
        g_readerRecord.Bytes(path->begin, path->end);
    } else {
        // A temporary file: its name changes from call to call; its bytes
        // are what was written.
        g_readerPath.assign(path->begin, path->end);
        std::string bytes;
        bool there = false;
        if (std::FILE* in = std::fopen(g_readerPath.c_str(), "rb")) {
            there = true;
            char buffer[256];
            std::size_t n = 0;
            while ((n = std::fread(buffer, 1, sizeof buffer, in)) > 0)
                bytes.append(buffer, n);
            std::fclose(in);
        }
        g_readerRecord.Byte(there ? 1 : 0);
        g_readerRecord.Bytes(bytes.data(), bytes.data() + bytes.size());
    }
    if (self->answer)
        *picture = self->answer;
    return self->result;
}

// A message of the test's own for the document's +4 part (slot 3): its type
// (slot 4), whether it succeeded (slot 14) and its request (slot 20), each
// call recorded.
struct MessageStand {
    void** vtable;
    uint32_t type;
    uint32_t succeeded;
    char* request;
};
uint32_t __fastcall MessageType(MessageStand* self, void*)
{
    g_readerRecord.Word(0x404);
    return self->type;
}
uint32_t __fastcall MessageSucceeded(MessageStand* self, void*)
{
    g_readerRecord.Word(0x40E);
    return self->succeeded;
}
char* __fastcall MessageRequest(MessageStand* self, void*)
{
    g_readerRecord.Word(0x414);
    return self->request;
}

void* g_readerVtable[8] = {};
void* g_messageVtable[24] = {};

void SetUpImageStands()
{
    FillStand(g_readerVtable, std::make_integer_sequence<int, 8>{});
    g_readerVtable[1] = reinterpret_cast<void*>(&ReaderRef);
    g_readerVtable[2] = reinterpret_cast<void*>(&ReaderRelease);
    g_readerVtable[4] = reinterpret_cast<void*>(&ReaderRead);
    FillStand(g_messageVtable, std::make_integer_sequence<int, 24>{});
    g_messageVtable[4] = reinterpret_cast<void*>(&MessageType);
    g_messageVtable[14] = reinterpret_cast<void*>(&MessageSucceeded);
    g_messageVtable[20] = reinterpret_cast<void*>(&MessageRequest);
}

char* TreeNext(char* node) { return reinterpret_cast<char*(__cdecl*)(char*)>(addr::SC4Tree_Next)(node); }

// The document's image map in order: each address, picture, the nodes
// waiting (their number) and the request.
void HashImageMap(Fnv& f, char* doc)
{
    char* const map = doc + 0x198;
    char* const header = At<char*>(map, 0x0);
    f.Word(At<uint32_t>(map, 0x4));
    uint32_t count = 0;
    for (char* node = At<char*>(header, 0x8); node != header && count < 1000; node = TreeNext(node), ++count) {
        f.Bytes(At<char*>(node, 0x14), At<char*>(node, 0x18));
        f.Word(StandNumber(At<void*>(node, 0x24)));
        HashField(f, node, {0x28, Field::Vector});
        f.Word(At<uint32_t>(node, 0x34));
    }
    f.Word(count);
}

// The IMG nodes of a tree: each one's picture, WIDTH and HEIGHT.
void HashImageNodes(Fnv& f, char* node, int depth)
{
    if (!node || depth > 64)
        return;
    const uint32_t vtable = At<uint32_t>(node, 0x0);
    if (vtable == 0x00ACE188) {
        f.Word(StandNumber(At<void*>(node, 0x24)));
        f.Word(At<uint32_t>(node, 0x2C));
        f.Word(At<uint32_t>(node, 0x30));
        return;
    }
    const NodeClass* const c = ClassOf(vtable);
    if (!c || !c->container)
        return;
    for (char** at = At<char**>(node, 0x1C); at && at < At<char**>(node, 0x20); ++at)
        HashImageNodes(f, *at, depth + 1);
}

// A page whose images wait: a.gif twice, b.gif once (in a paragraph, with a
// WIDTH), and LINK and META in the head.
const char* const kImagePage =
    "<HTML><HEAD><LINK REL=a HREF=s.css><META NAME=n CONTENT=c></HEAD><BODY><IMG SRC=a.gif><P><IMG SRC=b.gif "
    "WIDTH=3></P><IMG SRC=\"a.gif\"></BODY></HTML>";
const char* const kImageBase = "http://maxis.com/";

// The image map's node whose address ends with `name`, or null.
char* ImageNodeOf(char* doc, const char* name)
{
    char* const header = At<char*>(doc + 0x198, 0x0);
    const std::size_t n = std::strlen(name);
    for (char* node = At<char*>(header, 0x8); node != header; node = TreeNext(node)) {
        const char* const first = At<char*>(node, 0x14);
        const char* const last = At<char*>(node, 0x18);
        if (static_cast<std::size_t>(last - first) >= n && std::memcmp(last - n, name, n) == 0)
            return node;
    }
    return nullptr;
}

// A request as the service answers one (0x44 bytes): the address (+8), the
// flag that the text at +0x30 (begin, end, storage) is a path (+0x2C).
void MakeRequest(char* request, const char* address, bool isPath, const char* text, std::size_t length)
{
    std::memset(request, 0, 0x44);
    MakeText(reinterpret_cast<GameText*>(request + 0x8), address);
    At<uint8_t>(request, 0x2C) = isPath ? 1 : 0;
    if (length) {
        char* const chars = static_cast<char*>(GameAllocate(length));
        std::memcpy(chars, text, length);
        At<char*>(request, 0x30) = chars;
        At<char*>(request, 0x34) = chars + length;
        At<char*>(request, 0x38) = chars + length;
    }
}

void FreeRequest(char* request)
{
    DestroyText(reinterpret_cast<GameText*>(request + 0x8));
    if (At<char*>(request, 0x30))
        GameFree(At<char*>(request, 0x30));
}

std::string AddressOf(char* node) { return std::string(At<char*>(node, 0x14), At<char*>(node, 0x18)); }

// Image entries made and destroyed: four addresses, each with an empty
// record and with a picture and three nodes waiting.
const char* const kEntryAddresses[] = {"http://maxis.com/a.gif", "", "x", "\xC3\xA9.png"};
constexpr int kEntryItems = static_cast<int>(std::size(kEntryAddresses)) * 2;

uint32_t ImageEntryItem(int which)
{
    Fnv f;
    GameText address;
    MakeText(&address, kEntryAddresses[which / 2]);
    uint32_t record[5] = {};
    char** waiting = nullptr;
    if (which % 2) {
        waiting = static_cast<char**>(GameAllocate(16));
        waiting[0] = static_cast<char*>(StandPtr(4));
        waiting[1] = static_cast<char*>(StandPtr(5));
        waiting[2] = nullptr;
        record[0] = StandAt(3);
        record[1] = static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(waiting));
        record[2] = record[1] + 12;
        record[3] = record[1] + 16;
    }
    record[4] = 0x70u + static_cast<uint32_t>(which);
    alignas(4) char entry[0x28];
    std::memset(entry, kPattern, sizeof entry);
    f.Byte(CallGameThis<char*>(addr::HTMLImageEntry_Construct, entry, static_cast<const GameText*>(&address),
                               static_cast<const void*>(record)) == entry);
    HashField(f, entry, {0x4, Field::Body});
    f.Word(StandNumber(At<void*>(entry, 0x14)));
    char** const first = At<char**>(entry, 0x18);
    char** const last = At<char**>(entry, 0x1C);
    f.Word(static_cast<uint32_t>(last - first));
    f.Word(static_cast<uint32_t>(At<char**>(entry, 0x20) - first));
    for (char** at = first; at && at < last; ++at)
        f.Word(StandNumber(*at));
    f.Word(At<uint32_t>(entry, 0x24));
    Blocks blocks;
    if (first)
        blocks.Track(first, static_cast<std::size_t>(At<char*>(entry, 0x20) - reinterpret_cast<char*>(first)), false);
    CallGameThis<void>(addr::HTMLImageEntry_Destroy, entry);
    HashFreedDeep(f, blocks);
    f.Word(At<uint32_t>(entry, 0x0));
    DestroyText(&address);
    if (waiting)
        GameFree(waiting);
    return f.h;
}

// The image map's tree: maps of the test's own (a header of 0x38 bytes, the
// count, the compare), filled through the insert-unless-there (0x00901191,
// which inserts with 0x0090104C) in four orders (one with addresses
// twice), every other entry given a picture and nodes waiting; then one
// before the others inserted on the left of the leftmost (x set) and one
// after them on the right of the rightmost (`where` set); then cleared,
// cleared again and destroyed, destroyed at once, or its right subtree
// erased and the rest destroyed, the nodes and storage checked for being
// freed.
const char* const kMapOrders[] = {"dbfaceg", "abcdefg", "gfedcba", "dbfdacegd"};
constexpr int kMapItems = static_cast<int>(std::size(kMapOrders)) * 3;

struct TestMap {
    char* header;
    uint32_t count;
    uint32_t compare;
};

void MakeMap(TestMap& map)
{
    map.header = static_cast<char*>(GameAllocate(0x38));
    std::memset(map.header, 0, 0x38);
    At<char*>(map.header, 0x8) = map.header;
    At<char*>(map.header, 0xC) = map.header;
    map.count = 0;
    map.compare = 0;
}

void HashMapTree(Fnv& f, char* node, char* parent, int depth)
{
    if (!node || depth > 64) {
        f.Byte(0xD0);
        return;
    }
    f.Byte(At<uint8_t>(node, 0x0));
    f.Byte(At<char*>(node, 0x4) == parent ? 1 : 0);
    f.Bytes(At<char*>(node, 0x14), At<char*>(node, 0x18));
    f.Word(StandNumber(At<void*>(node, 0x24)));
    HashField(f, node, {0x28, Field::Vector});
    HashMapTree(f, At<char*>(node, 0x8), node, depth + 1);
    HashMapTree(f, At<char*>(node, 0xC), node, depth + 1);
}

void HashMap(Fnv& f, const TestMap& map)
{
    char* const header = map.header;
    f.Word(map.count);
    f.Byte(At<uint8_t>(header, 0x0));
    for (const std::size_t at : {0x8u, 0xCu}) {
        char* const end = At<char*>(header, at);
        if (end == header)
            f.Byte(0xEE);
        else
            f.Bytes(At<char*>(end, 0x14), At<char*>(end, 0x18));
    }
    HashMapTree(f, At<char*>(header, 0x4), header, 0);
}

void MakeEntry(char* entry, GameText* address, const std::string& text, uint32_t request)
{
    MakeText(address, text.c_str());
    uint32_t record[5] = {0, 0, 0, 0, request};
    CallGameThis<char*>(addr::HTMLImageEntry_Construct, entry, static_cast<const GameText*>(address),
                        static_cast<const void*>(record));
}

void CollectMapNodes(char* node, std::vector<char*>& nodes, int depth)
{
    if (!node || depth > 64)
        return;
    nodes.push_back(node);
    CollectMapNodes(At<char*>(node, 0x8), nodes, depth + 1);
    CollectMapNodes(At<char*>(node, 0xC), nodes, depth + 1);
}

void TrackNodes(Blocks& blocks, const std::vector<char*>& nodes)
{
    for (char* node : nodes) {
        blocks.Track(node, 0x38, false);
        if (char* const first = At<char*>(node, 0x28))
            blocks.Track(first, static_cast<std::size_t>(At<char*>(node, 0x30) - first), false);
    }
}

uint32_t ImageMapItem(int which)
{
    Fnv f;
    TestMap map;
    MakeMap(map);
    const std::string order = kMapOrders[which / 3];
    for (std::size_t i = 0; i < order.size(); ++i) {
        alignas(4) char entry[0x28];
        GameText address;
        MakeEntry(entry, &address, std::string("http://maxis.com/") + order[i] + ".gif", static_cast<uint32_t>(i));
        struct {
            char* node;
            uint32_t inserted;
        } result{};
        CallGameThis<void*>(addr::HTMLImageMap_InsertUnique, &map, static_cast<void*>(&result),
                            static_cast<const void*>(entry));
        CallGameThis<void>(addr::HTMLImageEntry_Destroy, entry);
        DestroyText(&address);
        f.Byte(static_cast<uint8_t>(result.inserted));
        f.Bytes(At<char*>(result.node, 0x14), At<char*>(result.node, 0x18));
        if ((result.inserted & 0xFF) && i % 2) {
            At<void*>(result.node, 0x24) = StandPtr(static_cast<int>(i % 8));
            for (std::size_t n = 0; n <= i % 3; ++n) {
                void* stand = StandPtr(static_cast<int>(8 + n));
                CallGameThis<void>(addr::SC4PtrVector_PushBack8FF785, result.node + 0x28, static_cast<void**>(&stand));
            }
        }
        HashMap(f, map);
    }
    for (int side = 0; side < 2; ++side) {
        alignas(4) char entry[0x28];
        GameText address;
        MakeEntry(entry, &address, side == 0 ? "http://maxis.com/0" : "http://maxis.com/~", 0x90u + side);
        char* const parent = At<char*>(map.header, side == 0 ? 0x8 : 0xC);
        char* made = nullptr;
        char** const r = CallGameThis<char**>(addr::HTMLImageMap_Insert, &map, &made,
                                              side == 0 ? reinterpret_cast<char*>(1) : nullptr, parent,
                                              static_cast<const char*>(entry),
                                              side == 1 ? reinterpret_cast<char*>(1) : nullptr);
        CallGameThis<void>(addr::HTMLImageEntry_Destroy, entry);
        DestroyText(&address);
        f.Byte(r == &made ? 1 : 0);
        f.Bytes(At<char*>(made, 0x14), At<char*>(made, 0x18));
        HashMap(f, map);
    }
    std::vector<char*> nodes;
    CollectMapNodes(At<char*>(map.header, 0x4), nodes, 0);
    Blocks blocks;
    switch (which % 3) {
    case 0:
        TrackNodes(blocks, nodes);
        CallGameThis<void>(addr::HTMLImageMap_Clear, &map);
        HashFreedDeep(f, blocks);
        HashMap(f, map);
        CallGameThis<void>(addr::HTMLImageMap_Clear, &map);
        HashMap(f, map);
        {
            Blocks header;
            header.Track(map.header, 0x38, false);
            CallGameThis<void>(addr::HTMLImageMap_Destroy, &map);
            HashFreedDeep(f, header);
        }
        break;
    case 1:
        TrackNodes(blocks, nodes);
        blocks.Track(map.header, 0x38, false);
        CallGameThis<void>(addr::HTMLImageMap_Destroy, &map);
        HashFreedDeep(f, blocks);
        break;
    default: {
        // Nothing erased for no node; then the root's right subtree, the
        // rest made a tree again (the root its rightmost) and destroyed.
        CallGameThis<void>(addr::HTMLImageMap_Erase, &map, static_cast<char*>(nullptr));
        HashMap(f, map);
        char* const root = At<char*>(map.header, 0x4);
        std::vector<char*> right;
        CollectMapNodes(At<char*>(root, 0xC), right, 0);
        TrackNodes(blocks, right);
        CallGameThis<void>(addr::HTMLImageMap_Erase, &map, At<char*>(root, 0xC));
        HashFreedDeep(f, blocks);
        At<char*>(root, 0xC) = nullptr;
        At<char*>(map.header, 0xC) = root;
        map.count -= static_cast<uint32_t>(right.size());
        HashMap(f, map);
        CallGameThis<void>(addr::HTMLImageMap_Destroy, &map);
        break;
    }
    }
    return f.h;
}

// The document's release, called by hand: on a document with images
// waiting (one of them with a stand-in picture), LINK, META and a reader of
// the test's own, and again; on one that was never Init'd; on one with no
// service and no images. The root, the base and the map's nodes checked for
// being freed.
const char* const kNoImagePage =
    "<HTML><HEAD><LINK REL=a HREF=s.css><LINK><META NAME=n CONTENT=c></HEAD><BODY><P>p</P></BODY></HTML>";
constexpr int kReleaseItems = 3;

uint32_t ReleaseItem(char* window, int which)
{
    using InitFn = uint8_t(__thiscall*)(void*, void*, void*, const GameText*);
    Fnv f;
    g_standRecord = Fnv{};
    g_readerRecord = Fnv{};
    g_serviceCancels = Fnv{};
    char* doc = nullptr;
    if (which == 1) {
        doc = NewDocument();
    } else if (which == 0) {
        doc = MakeSlotDocument(window, kImageBase, kImagePage);
    } else {
        doc = NewDocument();
        GameText text;
        MakeText(&text, kImageBase);
        reinterpret_cast<InitFn>(addr::HTMLDocument_Init)(doc, window, nullptr, &text);
        DestroyText(&text);
        MakeText(&text, kNoImagePage);
        Virtual<uint8_t>(doc, 0x20, &text);
        DestroyText(&text);
    }
    ReaderStand reader{g_readerVtable, 0x5800, 1, nullptr, 1};
    if (which == 0) {
        At<void*>(ImageNodeOf(doc, "b.gif"), 0x24) = StandPtr(6);
        At<ReaderStand*>(doc, 0x1A4) = &reader;
    }
    Blocks blocks;
    if (At<char*>(doc, 0x14))
        blocks.Track(At<char*>(doc, 0x14), 0x44, true);
    if (At<char*>(doc, 0xB8))
        blocks.Track(At<char*>(doc, 0xB8), 0x14, true);
    std::vector<char*> nodes;
    CollectMapNodes(At<char*>(At<char*>(doc, 0x198), 0x4), nodes, 0);
    TrackNodes(blocks, nodes);
    f.Word(static_cast<uint32_t>(blocks.blocks.size()));
    for (int pass = 0; pass < 2; ++pass) {
        const uint8_t r = CallGameThis<uint8_t>(addr::HTMLDocument_ReleaseImages, doc);
        if (pass == 0)
            HashFreedDeep(f, blocks);
        f.Byte(r & 1);
        f.Byte(At<uint8_t>(doc, 0x18));
        for (const std::size_t at : {0x14u, 0xB8u, 0x1A4u, 0x30u, 0x2Cu})
            f.Byte(At<void*>(doc, at) != nullptr);
        for (const std::size_t at : {0xACu, 0xBCu})
            HashField(f, doc, {static_cast<uint16_t>(at), Field::Vector});
        HashImageMap(f, doc);
        f.Word(g_standRecord.h);
        f.Word(g_readerRecord.h);
        f.Word(g_serviceCancels.h);
    }
    Virtual<uint32_t>(doc, 0x8);
    return f.h;
}

// The picture reader: one of the test's own giving a picture to none and
// none over one; the game's, made on first use, for a file that is not
// there, twice.
constexpr int kReadItems = 3;

uint32_t ReadItem(char* window, int which)
{
    using ReadFn = uint32_t(__thiscall*)(void*, const GameText*, void**);
    Fnv f;
    g_readerRecord = Fnv{};
    char* const doc = MakeSlotDocument(window, kImageBase, nullptr);
    ReaderStand reader{g_readerVtable, 0x5810, 1, which == 0 ? StandPtr(7) : nullptr, which == 0 ? 1u : 0u};
    if (which < 2)
        At<ReaderStand*>(doc, 0x1A4) = &reader;
    GameText path;
    MakeText(&path, which < 2 ? "C:/images/a.gif" : "opensc4-h2b2-no-such-image.gif");
    for (int pass = 0; pass < (which < 2 ? 1 : 2); ++pass) {
        void* picture = which == 1 ? StandPtr(8) : nullptr;
        const uint32_t r = reinterpret_cast<ReadFn>(addr::HTMLDocument_ReadPicture)(doc, &path, &picture);
        f.Byte(static_cast<uint8_t>(r));
        f.Word(picture && StandNumber(picture) == 0xEEEEu ? 0xEEEEu : StandNumber(picture));
        f.Byte(At<void*>(doc, 0x1A4) != nullptr);
        if (picture && StandNumber(picture) == 0xEEEEu)
            Virtual<uint32_t>(picture, 0x8);
    }
    DestroyText(&path);
    f.Word(g_readerRecord.h);
    Virtual<uint32_t>(doc, 0x8);
    return f.h;
}

// An image loaded (0x008FFBFC): from a path with a picture and without, from
// bytes (written to a temporary file the reader reads, removed after) and
// from none, for an address not in the map, and twice for one address.
constexpr int kLoadItems = 6;
const char kImageBytes[] = "GIF89a\x01\x00\x02\x00\x80\xff\x00binary";

void LoadOne(char* doc, const std::string& address, bool isPath, const char* text, std::size_t length)
{
    alignas(4) char request[0x44];
    MakeRequest(request, address.c_str(), isPath, text, length);
    CallGameThis<void>(addr::HTMLDocument_LoadImage, doc, static_cast<char*>(request));
    FreeRequest(request);
}

uint32_t LoadItem(char* window, int which)
{
    Fnv f;
    g_standRecord = Fnv{};
    g_readerRecord = Fnv{};
    char* const doc = MakeSlotDocument(window, kImageBase, kImagePage);
    const std::string a = AddressOf(ImageNodeOf(doc, "a.gif"));
    const std::string b = AddressOf(ImageNodeOf(doc, "b.gif"));
    ReaderStand reader{g_readerVtable, 0x5820, 1, nullptr, 1};
    At<ReaderStand*>(doc, 0x1A4) = &reader;
    switch (which) {
    case 0:
        reader.answer = StandPtr(9);
        LoadOne(doc, a, true, "C:/cache/a.gif", 14);
        break;
    case 1:
        LoadOne(doc, b, true, "C:/cache/b.gif", 14);
        break;
    case 2:
    case 3:
        reader.mode = 2;
        reader.answer = StandPtr(which == 2 ? 10 : 11);
        g_readerPath.clear();
        if (which == 2)
            LoadOne(doc, a, false, kImageBytes, sizeof kImageBytes - 1);
        else
            LoadOne(doc, b, false, nullptr, 0);
        f.Byte(!g_readerPath.empty() && GetFileAttributesA(g_readerPath.c_str()) == INVALID_FILE_ATTRIBUTES ? 1 : 0);
        break;
    case 4:
        reader.answer = StandPtr(9);
        LoadOne(doc, "http://maxis.com/c.gif", true, "C:/cache/c.gif", 14);
        break;
    default:
        reader.answer = StandPtr(12);
        LoadOne(doc, a, true, "a1", 2);
        reader.answer = StandPtr(13);
        LoadOne(doc, a, true, "a2", 2);
        break;
    }
    f.Word(g_readerRecord.h);
    f.Word(g_standRecord.h);
    HashImageMap(f, doc);
    HashImageNodes(f, At<char*>(doc, 0x14), 0);
    Virtual<uint32_t>(doc, 0x8);
    return f.h;
}

// The service's answer through the +4 part's slot 3: another type, one
// that failed, one that succeeded, one that succeeded for an address not in
// the map.
constexpr int kMessageItems = 4;

uint32_t MessageItem(char* window, int which)
{
    Fnv f;
    g_standRecord = Fnv{};
    g_readerRecord = Fnv{};
    char* const doc = MakeSlotDocument(window, kImageBase, kImagePage);
    ReaderStand reader{g_readerVtable, 0x5830, 1, StandPtr(14), 1};
    At<ReaderStand*>(doc, 0x1A4) = &reader;
    alignas(4) char request[0x44];
    MakeRequest(request, which == 3 ? "http://maxis.com/c.gif" : AddressOf(ImageNodeOf(doc, "a.gif")).c_str(), true,
                "C:/m.gif", 8);
    MessageStand message{g_messageVtable, which == 0 ? 0x12345678u : 0x75758483u, which == 1 ? 0u : which == 3 ? 2u : 1u,
                         request};
    f.Byte(Virtual<uint8_t>(doc + 4, 0xC, &message) & 1);
    FreeRequest(request);
    f.Word(g_readerRecord.h);
    f.Word(g_standRecord.h);
    HashImageMap(f, doc);
    HashImageNodes(f, At<char*>(doc, 0x14), 0);
    Virtual<uint32_t>(doc, 0x8);
    return f.h;
}

// Bullets: each item on a document (no root, a plain body, a body with
// BGCOLOR) with the shared bullets put aside and an empty list in their
// place, a sequence of font sizes, colours and kinds (cache hits among
// them, and a bullet in the list with no picture); each node and its
// picture (size and pixels, through its slots 9, 10, 34 and 35) hashed, then
// the list; the pictures released and the game's list put back.
struct BulletCall {
    uint32_t size;
    uint32_t colour;
    uint32_t kind;
};
struct BulletCase {
    int page;
    bool planted;
    std::vector<BulletCall> calls;
};
const std::vector<BulletCase>& BulletCases()
{
    static const std::vector<BulletCase> cases = {
        {1,
         false,
         {{28, 0xFF000000u, 0}, {28, 0xFF000000u, 0}, {28, 0xFF000000u, 1}, {29, 0xFF000000u, 0}, {0, 0xFF0000FFu, 0},
          {3, 0xFF00FF00u, 0}, {5, 0xFF00FF00u, 0}}},
        {0, false, {{14, 0xFFFFFFFFu, 0}, {36, 0xFF808080u, 3}}},
        {2, false, {{20, 0xFF102030u, 0}, {20, 0xFF102030u, 2}}},
        {1, true, {{14, 0xFF445566u, 0}, {14, 0xFF445566u, 0}}},
    };
    return cases;
}
constexpr int kBulletItems = 4;

uint32_t PictureIndex(std::vector<void*>& seen, void* picture)
{
    if (!picture)
        return 0xFFFFFFFFu;
    for (std::size_t i = 0; i < seen.size(); ++i)
        if (seen[i] == picture)
            return static_cast<uint32_t>(i);
    seen.push_back(picture);
    return static_cast<uint32_t>(seen.size() - 1);
}

void HashPicture(Fnv& f, void* picture)
{
    const uint32_t width = Virtual<uint32_t>(picture, 0x24);
    const uint32_t height = Virtual<uint32_t>(picture, 0x28);
    f.Word(width);
    f.Word(height);
    const char* const pixels = Virtual<const char*>(picture, 0x88);
    const uint32_t pitch = Virtual<uint32_t>(picture, 0x8C);
    f.Byte(pixels != nullptr);
    if (!pixels || width > 256 || height > 256)
        return;
    for (uint32_t y = 0; y < height; ++y)
        f.Bytes(pixels + y * pitch, pixels + y * pitch + width * 2);
}

uint32_t BulletItem(char* window, int which)
{
    const BulletCase& c = BulletCases()[static_cast<std::size_t>(which)];
    static const char* const pages[] = {nullptr, "<HTML><BODY>x</BODY></HTML>",
                                        "<HTML><BODY BGCOLOR=\"#123456\">x</BODY></HTML>"};
    Fnv f;
    char* const doc = MakeSlotDocument(window, kImageBase, pages[c.page]);
    uint32_t* const bullets = reinterpret_cast<uint32_t*>(static_cast<std::uintptr_t>(0x00B62524));
    uint32_t saved[3];
    std::memcpy(saved, bullets, sizeof saved);
    std::memset(bullets, 0, sizeof saved);
    if (c.planted) {
        uint32_t* const storage = static_cast<uint32_t*>(GameAllocate(16));
        storage[0] = 7;
        storage[1] = 0;
        storage[2] = 0xFF445566u;
        storage[3] = 0;
        bullets[0] = static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(storage));
        bullets[1] = bullets[0] + 16;
        bullets[2] = bullets[0] + 16;
    }
    std::vector<void*> seen;
    for (std::size_t i = 0; i < c.calls.size(); ++i) {
        const BulletCall& call = c.calls[i];
        At<uint32_t>(doc, 0x148) = call.size;
        At<uint32_t>(doc, 0x14C) = call.colour;
        At<uint32_t>(doc, 0x158) = 0x30u + static_cast<uint32_t>(i);
        char* const node = CallGameThis<char*>(addr::HTMLDocument_MakeBullet, doc, call.kind);
        f.Byte(node != nullptr);
        if (!node)
            continue;
        f.Word(At<uint32_t>(node, 0x0));
        for (std::size_t at = 0x1C; at < 0x3C; at += 4)
            f.Word(at == 0x24 ? PictureIndex(seen, At<void*>(node, at)) : At<uint32_t>(node, at));
        if (void* const picture = At<void*>(node, 0x24))
            HashPicture(f, picture);
        Virtual<char*>(node, 0x0, uint32_t{1});
    }
    const uint32_t* const first = reinterpret_cast<const uint32_t*>(static_cast<std::uintptr_t>(bullets[0]));
    const uint32_t* const last = reinterpret_cast<const uint32_t*>(static_cast<std::uintptr_t>(bullets[1]));
    f.Word(static_cast<uint32_t>(last - first));
    for (const uint32_t* at = first; at && at < last; at += 4) {
        for (int k = 0; k < 3; ++k)
            f.Word(at[k]);
        void* const picture = reinterpret_cast<void*>(static_cast<std::uintptr_t>(at[3]));
        f.Word(PictureIndex(seen, picture));
        if (picture)
            Virtual<uint32_t>(picture, 0x8);
    }
    if (first)
        GameFree(const_cast<uint32_t*>(first));
    std::memcpy(bullets, saved, sizeof saved);
    Virtual<uint32_t>(doc, 0x8);
    return f.h;
}

// Slot 5 (Parse again) with three pages, and slot 20 with four values of
// +0x2A and after a page with BLINK.
const char* const kSlot5Pages[] = {kSlotPage, "plain text, no tags", "<!DOCTYPE HTML><HTML><BODY>d</BODY></HTML>"};
const uint8_t kBlinkFlags[] = {0, 1, 2, 0xFF};
constexpr int kVtableSlotItems = static_cast<int>(std::size(kSlot5Pages)) + static_cast<int>(std::size(kBlinkFlags)) + 1;

uint32_t VtableSlotItem(char* window, int which)
{
    Fnv f;
    if (which < static_cast<int>(std::size(kSlot5Pages))) {
        char* const doc = MakeSlotDocument(window, "", nullptr);
        GameText text;
        MakeText(&text, kSlot5Pages[which]);
        f.Byte(static_cast<uint8_t>(Virtual<uint32_t>(doc, 0x14, &text)));
        DestroyText(&text);
        HashDocument(f, doc);
        Virtual<uint32_t>(doc, 0x8);
        return f.h;
    }
    which -= static_cast<int>(std::size(kSlot5Pages));
    char* const doc =
        MakeSlotDocument(window, "", which < static_cast<int>(std::size(kBlinkFlags)) ? nullptr
                                                                                      : "<BODY><BLINK>b</BLINK></BODY>");
    if (which < static_cast<int>(std::size(kBlinkFlags)))
        At<uint8_t>(doc, 0x2A) = kBlinkFlags[which];
    f.Byte(At<uint8_t>(doc, 0x2A));
    f.Byte(Virtual<uint8_t>(doc, 0x50));
    Virtual<uint32_t>(doc, 0x8);
    return f.h;
}

// h1a's: the HTML window's life, its pages, its COM slots and settings.
//
// Each item makes a window of its own through the class factory (never
// created on screen) and gives it a copy of its vtable in which the slots
// the window calls on itself that reach the screen or its parent are
// recorders: 6 (the window manager, a stand-in), 16 (a child taken out),
// 18, 70, 74 (the title), 91, 92 (drawn again), 96, 100 and 145 (a message
// to the parent). The fonts the manager gives are node stand-ins; the
// Internet service is the parser test's stand-in and the picture reader a
// stand-in of h2b2's.

Fnv g_winRecord;
void* g_winVtable[151] = {};
void* g_inputVtable[151] = {}; // h1b's copy of it (below)

template <int Slot>
uint32_t __fastcall WinRecord0(char*, void*)
{
    g_winRecord.Word(0x900u + Slot);
    return 0x9000u + Slot;
}

template <int Slot>
uint32_t __fastcall WinRecord1(char*, void*, uint32_t argument)
{
    g_winRecord.Word(0x900u + Slot);
    g_winRecord.Word(argument);
    return 0x9000u + Slot;
}

uint32_t __fastcall WinTitle(char*, void*, const GameText* title)
{
    g_winRecord.Word(0x900u + 74);
    g_winRecord.Bytes(title->begin, title->end);
    return 1;
}

char* g_winCurrent = nullptr;

uint32_t __fastcall WinPost(char*, void*, uint32_t target, const uint32_t* message)
{
    g_winRecord.Word(0x900u + 145);
    g_winRecord.Word(target);
    g_winRecord.Word(message[0]);
    g_winRecord.Byte(reinterpret_cast<char*>(static_cast<std::uintptr_t>(message[1])) == g_winCurrent);
    g_winRecord.Word(message[2]);
    g_winRecord.Word(message[3]);
    return 0x14500u;
}

// The window manager (slot 6): its slot 32 the font list, whose slot 9
// gives each font asked for from g_winFonts (null for none).
struct WinStand {
    void** vtable;
};
void* g_winFonts[3] = {};
void* g_fontListVtable[16] = {};
void* g_managerVtable[40] = {};
WinStand g_fontList{g_fontListVtable};
WinStand g_manager{g_managerVtable};

void* __fastcall FontListFont(WinStand*, void*, uint32_t id)
{
    g_winRecord.Word(0xF0u);
    g_winRecord.Word(id);
    return id == 0x12 ? g_winFonts[0] : id == 0x11 ? g_winFonts[1] : g_winFonts[2];
}
void* __fastcall ManagerFonts(WinStand*, void*)
{
    g_winRecord.Word(0xF1u);
    return &g_fontList;
}
void* __fastcall WinManager(char*, void*)
{
    g_winRecord.Word(0x906u);
    return &g_manager;
}

// A scroll bar of the test's own: slot 3 its window (a number), slot 2 its
// release.
struct BarStand {
    void** vtable;
    uint32_t id;
};
void* g_barVtable[8] = {};
BarStand g_bars[2] = {};
uint32_t __fastcall BarWindow(BarStand* self, void*)
{
    g_winRecord.Word(self->id);
    g_winRecord.Word(3);
    return self->id + 0x10000u;
}
uint32_t __fastcall BarRelease(BarStand* self, void*)
{
    g_winRecord.Word(self->id);
    g_winRecord.Word(2);
    return 0;
}

// The service's answers to the window (the +0xDC part's slot 3): h2b2's
// message stand-in with slot 10 recorded too.
void* g_winMessageVtable[24] = {};
uint32_t __fastcall WinMessageSlot10(MessageStand*, void*)
{
    g_readerRecord.Word(0x40A);
    return 0;
}

ReaderStand g_winReader{};

// The window's timer (+0x1DC) with a vtable whose start (slot 5), stop (7),
// running (8) and callback (9) are recorders, the timer service never
// asked: a window of the test's own never has a callback run.
constexpr std::uintptr_t kTimerVtable = 0x00AD1F14;
void* g_timerVtable[12] = {};
uint8_t g_timerRunning = 0;
uint32_t __fastcall TimerStart(char*, void*, uint32_t ms, uint32_t repeat)
{
    g_winRecord.Word(0x7105u);
    g_winRecord.Word(ms);
    g_winRecord.Word(repeat);
    g_timerRunning = 1;
    return 1;
}
uint32_t __fastcall TimerStop(char*, void*)
{
    g_winRecord.Word(0x7107u);
    g_timerRunning = 0;
    return 1;
}
uint8_t __fastcall TimerRunning(char*, void*) { return g_timerRunning; }
uint32_t __fastcall TimerCallback(char*, void*, uint32_t callback, char* data)
{
    g_winRecord.Word(0x7109u);
    g_winRecord.Byte(callback == static_cast<uint32_t>(addr::HTMLWindow_OnTimer));
    g_winRecord.Byte(data == g_winCurrent);
    return 1;
}

void SetUpWindowStands()
{
    std::memcpy(g_winVtable, reinterpret_cast<const void*>(static_cast<std::uintptr_t>(0x00ACD0D8)), sizeof g_winVtable);
    g_winVtable[6] = reinterpret_cast<void*>(&WinManager);
    g_winVtable[16] = reinterpret_cast<void*>(&WinRecord1<16>);
    g_winVtable[18] = reinterpret_cast<void*>(&WinRecord0<18>);
    g_winVtable[70] = reinterpret_cast<void*>(&WinRecord0<70>);
    g_winVtable[74] = reinterpret_cast<void*>(&WinTitle);
    g_winVtable[91] = reinterpret_cast<void*>(&WinRecord0<91>);
    g_winVtable[92] = reinterpret_cast<void*>(&WinRecord0<92>);
    g_winVtable[96] = reinterpret_cast<void*>(&WinRecord0<96>);
    g_winVtable[100] = reinterpret_cast<void*>(&WinRecord1<100>);
    g_winVtable[145] = reinterpret_cast<void*>(&WinPost);
    FillStand(g_fontListVtable, std::make_integer_sequence<int, 16>{});
    g_fontListVtable[9] = reinterpret_cast<void*>(&FontListFont);
    FillStand(g_managerVtable, std::make_integer_sequence<int, 40>{});
    g_managerVtable[32] = reinterpret_cast<void*>(&ManagerFonts);
    FillStand(g_barVtable, std::make_integer_sequence<int, 8>{});
    g_barVtable[2] = reinterpret_cast<void*>(&BarRelease);
    g_barVtable[3] = reinterpret_cast<void*>(&BarWindow);
    g_bars[0] = {g_barVtable, 0x5B00};
    g_bars[1] = {g_barVtable, 0x5B01};
    std::memcpy(g_winMessageVtable, g_messageVtable, sizeof g_winMessageVtable);
    g_winMessageVtable[10] = reinterpret_cast<void*>(&WinMessageSlot10);
    g_winReader = {g_readerVtable, 0x5D00, 1, nullptr, 0};
    std::memcpy(g_timerVtable, reinterpret_cast<const void*>(kTimerVtable), sizeof g_timerVtable);
    g_timerVtable[5] = reinterpret_cast<void*>(&TimerStart);
    g_timerVtable[7] = reinterpret_cast<void*>(&TimerStop);
    g_timerVtable[8] = reinterpret_cast<void*>(&TimerRunning);
    g_timerVtable[9] = reinterpret_cast<void*>(&TimerCallback);
}

// A window of the test's own: its +0xD8 interface with a reference, the
// recording vtable, the stand-in service.
char* NewTestWindow()
{
    char* const face = reinterpret_cast<char*(__cdecl*)()>(addr::HTMLWindow_Create)();
    Virtual<uint32_t>(face, 0x4);
    char* const window = face - 0xD8;
    At<void**>(window, 0x0) = g_winVtable;
    At<void*>(window, 0x460) = &g_service;
    At<void**>(window, 0x1DC) = g_timerVtable;
    g_timerRunning = 0;
    g_winCurrent = window;
    return window;
}

// The document let go (its images released, which releases the window and
// the service, then the document), the timer stopped, the window released.
void FreeTestWindow(char* window)
{
    if (char* const doc = At<char*>(window, 0x148)) {
        reinterpret_cast<uint8_t(__thiscall*)(void*)>(addr::HTMLDocument_ReleaseImages)(doc);
        Virtual<uint32_t>(doc, 0x8);
        At<char*>(window, 0x148) = nullptr;
    }
    At<uint32_t>(window, 0x1DC) = static_cast<uint32_t>(kTimerVtable);
    At<void*>(window, 0x460) = nullptr;
    Virtual<uint32_t>(window + 0xD8, 0x8);
}

// A word of the window: 0, an offset into the window, a stand-in's number,
// a heap pointer as one, anything else as it is.
uint32_t WindowWord(char* window, uint32_t value)
{
    const auto at = [](const void* p) { return static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(p)); };
    if (value == 0)
        return 0;
    if (value >= at(window) && value < at(window) + 0x468)
        return 0xC1000000u + (value - at(window));
    if (value == at(g_winVtable))
        return 0xC2000001u;
    if (value == at(g_inputVtable))
        return 0xC2000004u;
    if (const Stand* const s = StandAt(value)) // h1b's stand-ins
        return 0xC4000000u | s->kind << 8 | s->id;
    if (value == at(&g_service))
        return 0xC2000002u;
    if (value == at(&g_winReader))
        return 0xC2000003u;
    if (value >= at(&g_bars[0]) && value < at(&g_bars[2]))
        return 0xC2000010u + (value - at(&g_bars[0]));
    const uint32_t first = StandAt(0);
    if (value >= first && value < first + sizeof g_nodeStands)
        return 0xC3000000u + (value - first);
    return IsHeap(value) ? 0xB2u : value;
}

std::string UrlText(char* url)
{
    const GameText* const text = reinterpret_cast<const GameText*(__thiscall*)(void*)>(addr::SC4Url_Text)(url);
    return std::string(text->begin, text->end);
}

void HashUrl(Fnv& f, char* url)
{
    const std::string text = UrlText(url);
    f.Word(static_cast<uint32_t>(text.size()));
    f.Bytes(text.data(), text.data() + text.size());
}

// The window's own fields (not the framework window's but its colour and
// area): the context, the document, the fonts, the string, the flags and
// margins, the scroll bars, the timer running, the steps, the addresses,
// the service and the load state.
void HashWindow(Fnv& f, char* window)
{
    f.Word(WindowWord(window, At<uint32_t>(window, 0x0)));
    for (std::size_t at = 0x24; at < 0x34; at += 4)
        f.Word(At<uint32_t>(window, at));
    f.Word(At<uint32_t>(window, 0xD4));
    for (std::size_t at = 0xD8; at < 0x14C; at += 4)
        f.Word(WindowWord(window, At<uint32_t>(window, at)));
    for (std::size_t at = 0x154; at < 0x16C; at += 4)
        f.Word(WindowWord(window, At<uint32_t>(window, at)));
    const GameText* const missing = &At<GameText>(window, 0x16C);
    f.Bytes(missing->begin, missing->end);
    for (std::size_t at = 0x180; at < 0x1DC; at += 4)
        f.Word(WindowWord(window, At<uint32_t>(window, at)));
    f.Byte(Virtual<uint8_t>(window + 0x1DC, 0x20));
    for (std::size_t at = 0x204; at < 0x220; at += 4)
        f.Word(At<uint32_t>(window, at));
    HashUrl(f, window + 0x220);
    HashUrl(f, window + 0x2E0);
    HashUrl(f, window + 0x3A0);
    f.Word(WindowWord(window, At<uint32_t>(window, 0x460)));
    f.Word(At<uint32_t>(window, 0x464));
    if (char* const doc = At<char*>(window, 0x148)) {
        f.Byte(At<char*>(window, 0xE4) == doc);
        HashDocument(f, doc);
        for (std::size_t at = 0x1A8; at < 0x1C4; at += 4)
            f.Word(At<uint32_t>(doc, at));
    }
}

// An address set from text (into +0x220, +0x2E0 or +0x3A0), as the window's
// slot 19 does it.
void SetUrl(char* url, const char* text)
{
    GameText from;
    MakeText(&from, text);
    alignas(4) char made[0xC0];
    reinterpret_cast<void*(__thiscall*)(void*, const void*)>(addr::SC4Url_FromText)(made, &from);
    reinterpret_cast<void*(__thiscall*)(void*, const void*)>(addr::SC4Url_Assign)(url, made);
    reinterpret_cast<void(__thiscall*)(void*)>(addr::SC4Url_Destroy)(made);
    DestroyText(&from);
}

// A page handed to slot 22 (a range of characters) with a base.
struct Range {
    const char* begin;
    const char* end;
};

uint8_t ShowPage(char* window, const char* page, const char* base)
{
    const Range range{page, page + std::strlen(page)};
    GameText text;
    MakeText(&text, base);
    const uint8_t shown = Virtual<uint8_t>(window + 0xD8, 0x58, &range, &text);
    DestroyText(&text);
    return shown;
}

const char* const kWinPages[] = {
    "<HTML><BODY>Hello <B>window</B></BODY></HTML>",
    "<HTML><BODY>text <BLINK>blink <B>b</B></BLINK> and more</BODY></HTML>",
    "plain text, no tags",
    "<BODY BGCOLOR=#102030><IMG SRC=\"a.png\" WIDTH=4 HEIGHT=4><A HREF=\"x.html\">x</A></BODY>",
    "",
};
const char* const kWinBases[] = {"", "http://maxis.com/a/b.html", "sc4://advice/1", "file.html"};
const uint32_t kWinIds[] = {1, 0x22BA0121u, 0x452294AAu, 0x4A11FD4Au, 0x12345678u, 0};

// Sequences of the four flag slots: slot (26 add, 27 set, 28 remove, 29
// test) and value.
struct FlagStep {
    int slot;
    uint32_t value;
};
const FlagStep kFlagSequences[][5] = {
    {{26, 0x8}, {26, 0x100}, {29, 0x100}, {28, 0x100}, {29, 0x100}},
    {{28, 0x8}, {28, 0x8}, {29, 0xC}, {26, 0x8}, {29, 0x8}},
    {{27, 0}, {29, 0xFFFFFFFFu}, {26, 0x3}, {28, 0x1}, {29, 0x3}},
    {{27, 0xFA}, {26, 0x6}, {28, 0x5}, {28, 0xFFFFFFFFu}, {29, 0xFA}},
    {{26, 0}, {28, 0}, {29, 0}, {27, 0xFFFFFFFFu}, {29, 0x80}},
    {{26, 0x80000000u}, {29, 0x80000000u}, {27, 0x80000000u}, {28, 0x7FFFFFFF}, {29, 0x80000000u}},
};
const int32_t kWinMargins[][4] = {{0, 0, 0, 0}, {10, 20, 30, 40}, {-5, 3, 7, -9}, {4, 4, 0x16, 4}};
const int32_t kWinSteps[][7] = {{8, 10, 12, 16, 19, 24, 48}, {1, 2, 3, 4, 5, 6, 7}, {0, 0, 0, 0, 0, 0, 0}};

// The OnMessage cases: type, succeeded, the load state before, the
// request's text, a page shown before.
struct MessageCase {
    uint32_t type;
    uint32_t succeeded;
    uint32_t state;
    const char* text;
    bool shown;
};
const MessageCase kWinMessages[] = {
    {0x12345678u, 1, 1, "<BODY>x</BODY>", false}, {0x75758483u, 1, 0, "<BODY>x</BODY>", false},
    {0x75758483u, 1, 1, "<BODY>answer <I>page</I></BODY>", false}, {0x75758483u, 1, 1, "", false},
    {0x75758483u, 1, 1, "", true}, {0x75758483u, 0, 1, "", false},
    {0x75758483u, 0, 1, "", true}, {0x75758483u, 1, 2, "<BODY>y</BODY>", true},
};

constexpr int kWinLifeItems = 3;
constexpr int kWinQueryItems = static_cast<int>(std::size(kWinIds)) * 3;
constexpr int kWinFlagItems = static_cast<int>(std::size(kFlagSequences));
constexpr int kWinMarginItems = static_cast<int>(std::size(kWinMargins)) * 2;
constexpr int kWinStepItems = static_cast<int>(std::size(kWinSteps)) + 3;
constexpr int kWinBaseItems = static_cast<int>(std::size(kWinBases));
constexpr int kWinSmallItems = 4 + 3 + 2 + 2 + 3 + 1 + 3;
constexpr int kWinPageItems = static_cast<int>(std::size(kWinPages)) + 4;
constexpr int kWinBarItems = 5;
constexpr int kWinMessageItems = static_cast<int>(std::size(kWinMessages));
constexpr int kWinTextItems = 1 + 2 + 2;
constexpr int kWinCreateItems = 3;
constexpr int kWindowItems = kWinLifeItems + kWinQueryItems + kWinFlagItems + kWinMarginItems + kWinStepItems +
                             kWinBaseItems + kWinSmallItems + kWinPageItems + kWinBarItems + kWinMessageItems +
                             kWinTextItems + kWinCreateItems;

uint32_t WindowItem(int which)
{
    Fnv f;
    g_winRecord = Fnv{};
    g_readerRecord = Fnv{};
    const auto offset = [](char* window, const void* p) {
        return WindowWord(window, static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(p)));
    };
    const auto finish = [&](char* window) {
        HashWindow(f, window);
        f.Word(g_winRecord.h);
        f.Word(g_readerRecord.h);
        FreeTestWindow(window);
        return f.h;
    };
    if (which < kWinLifeItems) {
        // Made by the factory, hashed and released; made on patterned
        // memory and deleted with flag 1 and with 0 (the block checked).
        if (which == 0) {
            char* const face = reinterpret_cast<char*(__cdecl*)()>(addr::HTMLWindow_Create)();
            char* const window = face - 0xD8;
            Virtual<uint32_t>(face, 0x4);
            HashWindow(f, window);
            Blocks blocks;
            blocks.Track(window, 0x468, true);
            f.Word(Virtual<uint32_t>(face, 0x8));
            HashFreedDeep(f, blocks);
            return f.h;
        }
        char* const memory = static_cast<char*>(GameNew(0x468));
        std::memset(memory, kPattern, 0x468);
        char* const window = CallGameThis<char*>(addr::HTMLWindow_Construct, memory);
        f.Word(offset(memory, window));
        HashWindow(f, memory);
        Blocks blocks;
        blocks.Track(memory, 0x468, true);
        f.Word(offset(memory, CallGameThis<char*>(addr::HTMLWindow_Delete, memory, uint32_t(which == 1 ? 1 : 0))));
        if (which == 2)
            GameDelete(memory);
        HashFreedDeep(f, blocks);
        return f.h;
    }
    which -= kWinLifeItems;
    if (which < kWinQueryItems) {
        // QueryInterface through the window, its +0xD8 and its +0xDC part;
        // the +0xDC part's AddRef and Release after.
        char* const window = NewTestWindow();
        char* const self = window + (which % 3 == 0 ? 0 : which % 3 == 1 ? 0xD8 : 0xDC);
        void* out = reinterpret_cast<void*>(0x77);
        const uint8_t r = Virtual<uint8_t>(self, 0x0, kWinIds[which / 3], &out);
        f.Byte(r & 1);
        f.Word(out == reinterpret_cast<void*>(0x77) ? 0xEEEEu : offset(window, out));
        f.Word(Virtual<uint32_t>(window + 0xDC, 0x4));
        f.Word(Virtual<uint32_t>(window + 0xDC, 0x8));
        if (r & 1)
            Virtual<uint32_t>(window, 0x8);
        return finish(window);
    }
    which -= kWinQueryItems;
    if (which < kWinFlagItems) {
        char* const window = NewTestWindow();
        for (const FlagStep& step : kFlagSequences[which]) {
            const uint32_t r = Virtual<uint32_t>(window + 0xD8, static_cast<std::size_t>(step.slot) * 4, step.value);
            f.Word(WindowWord(window, r)); // slot 27's answer (and 26's and 28's through it) is an address
            f.Word(At<uint32_t>(window, 0x180));
        }
        return finish(window);
    }
    which -= kWinFlagItems;
    if (which < kWinMarginItems) {
        // The margins on an empty window and on one showing a page.
        char* const window = NewTestWindow();
        if (which % 2)
            f.Byte(ShowPage(window, kWinPages[0], ""));
        const int32_t* const m = kWinMargins[which / 2];
        const uint32_t r = Virtual<uint32_t>(window + 0xD8, 0x80, m[0], m[1], m[2], m[3]);
        f.Word(which % 2 ? r : offset(window, reinterpret_cast<void*>(static_cast<std::uintptr_t>(r))));
        return finish(window);
    }
    which -= kWinMarginItems;
    if (which < kWinStepItems) {
        char* const window = NewTestWindow();
        if (which < static_cast<int>(std::size(kWinSteps))) {
            // Slot 33, then a page shown with the steps.
            f.Word(offset(window, Virtual<int32_t*>(window + 0xD8, 0x84, kWinSteps[which])));
            f.Byte(ShowPage(window, "<BODY><FONT SIZE=7>big</FONT><FONT SIZE=1>small</FONT></BODY>", ""));
        } else {
            // The copy alone: none, three and all seven into a patterned
            // buffer (the window's steps).
            const int n = (which - static_cast<int>(std::size(kWinSteps))) == 0 ? 0 : which == 4 ? 3 : 7;
            int32_t* const to = &At<int32_t>(window, 0x204);
            for (int i = 0; i < 7; ++i)
                to[i] = 0x5A5A5A5A;
            int32_t* const r = reinterpret_cast<int32_t*(__cdecl*)(const int32_t*, const int32_t*, int32_t*)>(
                addr::HTMLWindow_CopyFontSizes)(kWinSteps[1], kWinSteps[1] + n, to);
            f.Word(offset(window, r));
        }
        return finish(window);
    }
    which -= kWinStepItems;
    if (which < kWinBaseItems) {
        // Slot 19, the base from text.
        char* const window = NewTestWindow();
        GameText text;
        MakeText(&text, kWinBases[which]);
        f.Byte(Virtual<uint8_t>(window + 0xD8, 0x4C, &text));
        DestroyText(&text);
        return finish(window);
    }
    which -= kWinBaseItems;
    if (which < kWinSmallItems) {
        char* const window = NewTestWindow();
        if (which < 4) {
            // Slots 15 and 16: nothing, a page asked for, both after it was
            // shown.
            if (which >= 1)
                SetUrl(window + 0x3A0, "http://maxis.com/asked.html");
            if (which >= 2)
                f.Byte(ShowPage(window, kWinPages[0], "http://maxis.com/"));
            if (which == 3)
                SetUrl(window + 0x3A0, "");
            for (const std::size_t slot : {0x3Cu, 0x40u}) {
                GameText out;
                MakeText(&out, "was");
                const uint32_t r = Virtual<uint32_t>(window + 0xD8, slot, &out);
                f.Byte(static_cast<uint8_t>(r));
                f.Bytes(out.begin, out.end);
                DestroyText(&out);
            }
            return finish(window);
        }
        which -= 4;
        if (which < 3) {
            // Slot 5, the load state.
            At<uint32_t>(window, 0x464) = which;
            f.Word(Virtual<uint32_t>(window + 0xD8, 0x14));
            return finish(window);
        }
        which -= 3;
        if (which < 2) {
            // Slot 6, the view's corner.
            At<uint32_t>(window, 0xFC) = which ? 17u : 0u;
            At<uint32_t>(window, 0x100) = which ? 0xFFFFFFF0u : 5u;
            uint32_t width = 0x99;
            uint32_t height = 0x98;
            f.Word(Virtual<uint32_t>(window + 0xD8, 0x18, &width, &height));
            f.Word(width);
            f.Word(height);
            return finish(window);
        }
        which -= 2;
        if (which < 2) {
            // Slot 23, the missing picture's name.
            GameText name;
            MakeText(&name, which ? "Other.jpg" : "");
            f.Word(offset(window, Virtual<void*>(window + 0xD8, 0x5C, &name)));
            DestroyText(&name);
            return finish(window);
        }
        which -= 2;
        if (which < 3) {
            // Slot 24, the load state posted, from 0, 1 and 2.
            At<uint32_t>(window, 0x464) = which;
            f.Word(Virtual<uint32_t>(window + 0xD8, 0x60));
            return finish(window);
        }
        which -= 3;
        if (which == 0) {
            f.Word(reinterpret_cast<uint32_t(__cdecl*)()>(addr::HTMLWindow_GetClassID)());
            return finish(window);
        }
        which -= 1;
        // Slot 107, the colour.
        const uint32_t colours[][3] = {{0, 0, 0}, {0xFF, 0x80, 0x01}, {0x1FF, 0x12345678u, 0xFFFFFFFFu}};
        f.Word(WindowWord(window, Virtual<uint32_t>(window, 0x1AC, colours[which][0], colours[which][1],
                                                    colours[which][2])));
        return finish(window);
    }
    which -= kWinSmallItems;
    if (which < kWinPageItems) {
        // Slot 22: each page once; then a page over a page, BLINK and after
        // it a plain one, a page with font steps, and ShowHTML alone.
        char* const window = NewTestWindow();
        const int pages = static_cast<int>(std::size(kWinPages));
        if (which < pages) {
            f.Byte(ShowPage(window, kWinPages[which], kWinBases[which % 4]));
        } else if (which == pages) {
            f.Byte(ShowPage(window, kWinPages[3], "http://maxis.com/"));
            f.Byte(ShowPage(window, kWinPages[0], ""));
        } else if (which == pages + 1) {
            f.Byte(ShowPage(window, kWinPages[1], ""));
            f.Byte(Virtual<uint8_t>(window + 0x1DC, 0x20));
            f.Byte(ShowPage(window, kWinPages[2], ""));
        } else if (which == pages + 2) {
            Virtual<int32_t*>(window + 0xD8, 0x84, kWinSteps[0]);
            f.Byte(ShowPage(window, kWinPages[1], ""));
            f.Byte(ShowPage(window, kWinPages[1], ""));
        } else {
            const char* const page = kWinPages[0];
            const Range range{page, page + std::strlen(page)};
            GameText base;
            MakeText(&base, "sc4://x");
            f.Byte(CallGameThis<uint8_t>(addr::HTMLWindow_ShowHTML, window, &range, &base));
            DestroyText(&base);
        }
        return finish(window);
    }
    which -= kWinPageItems;
    if (which < kWinBarItems) {
        // The scroll bars dropped: both, one, the other, none asked for, and
        // none there (with a page, laid out again).
        char* const window = NewTestWindow();
        if (which < 4) {
            At<void*>(window, 0x1A4) = &g_bars[0];
            At<void*>(window, 0x1A8) = &g_bars[1];
        } else {
            f.Byte(ShowPage(window, kWinPages[0], ""));
            At<void*>(window, 0x1A8) = &g_bars[1];
        }
        CallGameThis<void>(addr::HTMLWindow_DropScrollBars, window, uint32_t(which == 0 || which == 1 || which == 4),
                           uint32_t(which == 0 || which == 2 || which == 4));
        f.Word(WindowWord(window, At<uint32_t>(window, 0x1A4)));
        f.Word(WindowWord(window, At<uint32_t>(window, 0x1A8)));
        At<void*>(window, 0x1A4) = nullptr;
        At<void*>(window, 0x1A8) = nullptr;
        return finish(window);
    }
    which -= kWinBarItems;
    if (which < kWinMessageItems) {
        // The service's answer through the +0xDC part's slot 3.
        const MessageCase& c = kWinMessages[which];
        char* const window = NewTestWindow();
        if (c.shown)
            f.Byte(ShowPage(window, kWinPages[0], ""));
        SetUrl(window + 0x3A0, "http://maxis.com/answer/");
        At<uint32_t>(window, 0x464) = c.state;
        alignas(4) char request[0x40] = {};
        At<const char*>(request, 0x30) = c.text;
        At<const char*>(request, 0x34) = c.text + std::strlen(c.text);
        MessageStand message{g_winMessageVtable, c.type, c.succeeded, request};
        f.Byte(Virtual<uint8_t>(window + 0xDC, 0xC, &message));
        return finish(window);
    }
    which -= kWinMessageItems;
    if (which < kWinTextItems) {
        char* const window = NewTestWindow();
        if (which == 0) {
            // The timer's callback.
            f.Word(reinterpret_cast<uint32_t(__cdecl*)(char*)>(addr::HTMLWindow_OnTimer)(window));
            return finish(window);
        }
        which -= 1;
        if (which < 2) {
            // Slot 21, the page's text (into a string's body).
            f.Byte(ShowPage(window, kWinPages[which ? 2 : 0], ""));
            GameText out;
            MakeText(&out, "was");
            f.Byte(Virtual<uint8_t>(window + 0xD8, 0x54, &out.begin));
            f.Bytes(out.begin, out.end);
            DestroyText(&out);
            return finish(window);
        }
        which -= 2;
        // The layout context's destructor: as made, and with a left and a
        // right entry pushed (their storage checked).
        alignas(4) char context[0x68];
        CallGameThis<void*>(addr::HTMLLayout_Construct, context);
        Blocks blocks;
        if (which) {
            const uint32_t left[4] = {1, 2, 3, 4};
            const uint32_t right[2] = {5, 6};
            CallGameThis<void>(addr::HTMLLayout_PushLeftEntry, context + 0x50, static_cast<const void*>(left));
            CallGameThis<void>(addr::HTMLLayout_PushRightEntry, context + 0x5C, static_cast<const void*>(right));
            f.Word((At<char*>(context, 0x54) - At<char*>(context, 0x50)));
            f.Word((At<char*>(context, 0x60) - At<char*>(context, 0x5C)));
        }
        CallGameThis<void>(addr::HTMLLayout_Destruct, context);
        return finish(window);
    }
    which -= kWinTextItems;
    // Slot 4 (create) with fonts 0x12, 0x11 and 1 there, only 1, and none;
    // again (created); slot 5 (destroy), again.
    char* const window = NewTestWindow();
    At<void*>(window, 0x1D8) = &g_winReader;
    g_winFonts[0] = which == 0 ? &g_nodeStands[20] : nullptr;
    g_winFonts[1] = which == 0 ? &g_nodeStands[21] : nullptr;
    g_winFonts[2] = which < 2 ? &g_nodeStands[22] : nullptr;
    g_standRecord = Fnv{};
    f.Byte(Virtual<uint8_t>(window, 0x10));
    HashWindow(f, window);
    f.Byte(Virtual<uint8_t>(window, 0x10));
    f.Byte(Virtual<uint8_t>(window, 0x14));
    f.Byte(Virtual<uint8_t>(window, 0x14));
    f.Word(g_standRecord.h);
    At<void*>(window, 0x460) = &g_service;
    return finish(window);
}

// h1b's: the window's keys and mouse, its view and scroll bars, its paint
// and its addresses.
//
// Each item has a window as h1a's do (NewTestWindow), with a copy of that
// vtable in which more of the window's own slots record: 14 (a child
// added), 16 (taken out), 67 (the window's flags: created when the item
// says so), 85 (the cursor), 90 and 98 (the framework's SetArea calls
// them), 121 (whether a point is in the window) and 145 (a message to the
// parent, taken when the item says so); slot 6 is the game's again (the
// window manager, whose fonts measure the text laid out). Its picture
// (+0x68) is a recording stand-in that refuses one lock; +0xE0 (the layout
// context's window) is the window, as slot 4 makes it; the area (+0x24,
// +0xA8) 320 by 200. The window factory (0x00913C72's singleton, put back
// after) gives recording scroll bars when the item says so, each with a
// window of its own; the stand-ins' calls are hashed with the window, the
// document by its nodes' boxes. The pages start with <HTML>: one that
// starts with <BODY> is shown as one plain text run.

Fnv g_inputRecord;
uint32_t g_windowFlags = 0;  // slot 67's: 0x4000 created
uint8_t g_inside = 1;        // slot 121's
uint8_t g_postTaken = 0;     // slot 145's
uint32_t g_serviceGoes = 1;  // the service's slot 16

uint32_t InputWord(char* window, uint32_t value) { return WindowWord(window, value); }

template <int Slot>
uint32_t __fastcall InputRecord1(char* self, void*, uint32_t argument)
{
    g_winRecord.Word(0xA00u + Slot);
    g_winRecord.Word(InputWord(self, argument));
    return 0xA000u + Slot;
}

template <int Slot>
uint32_t __fastcall InputRecord0(char*, void*)
{
    g_winRecord.Word(0xA00u + Slot);
    return 0xA000u + Slot;
}

uint8_t __fastcall InputFlags(char*, void*, uint32_t flags)
{
    g_winRecord.Word(0xA43u);
    g_winRecord.Word(flags);
    return (g_windowFlags & flags) != 0 ? 1 : 0;
}

uint8_t __fastcall InputInside(char*, void*, int32_t x, int32_t y)
{
    g_winRecord.Word(0xA79u);
    g_winRecord.Word(static_cast<uint32_t>(x));
    g_winRecord.Word(static_cast<uint32_t>(y));
    return g_inside;
}

uint32_t __fastcall InputCursor(char* self, void*, uint32_t cursor, uint32_t show)
{
    g_winRecord.Word(0xA55u);
    g_winRecord.Word(InputWord(self, cursor));
    g_winRecord.Word(show & 0xFF);
    return 1;
}

// A message to the parent: a link's text (a game string) by its text.
uint8_t __fastcall InputPost(char* self, void*, uint32_t target, const uint32_t* message)
{
    g_winRecord.Word(0xA91u);
    g_winRecord.Word(target);
    g_winRecord.Word(message[0]);
    g_winRecord.Byte(reinterpret_cast<char*>(static_cast<std::uintptr_t>(message[1])) == self);
    if (message[0] == 0xE4A85D91u && message[2]) {
        const GameText* const text = reinterpret_cast<const GameText*>(static_cast<std::uintptr_t>(message[2]));
        g_winRecord.Bytes(text->begin, text->end);
    } else {
        g_winRecord.Word(InputWord(self, message[2]));
    }
    g_winRecord.Word(message[3]);
    return g_postTaken;
}

// The service's slot 16 (a page asked for): the address, the +0xDC part,
// the two words and the byte whether it is a new page.
uint32_t __fastcall ServiceGo(Service*, void*, char* url, char* target, uint32_t a, uint32_t b, uint32_t fresh)
{
    const std::string text = UrlText(url);
    g_winRecord.Word(0x5016u);
    g_winRecord.Bytes(text.data(), text.data() + text.size());
    g_winRecord.Byte(target == g_winCurrent + 0xDC);
    g_winRecord.Word(a);
    g_winRecord.Word(b);
    g_winRecord.Byte(static_cast<uint8_t>(fresh));
    return g_serviceGoes;
}

void SetUpInputStands()
{
    MakeStandTables();
    std::memcpy(g_inputVtable, g_winVtable, sizeof g_inputVtable);
    // The window manager the game's (its fonts measure the text laid out).
    g_inputVtable[6] = reinterpret_cast<void* const*>(static_cast<std::uintptr_t>(0x00ACD0D8))[6];
    g_inputVtable[14] = reinterpret_cast<void*>(&InputRecord1<14>);
    g_inputVtable[16] = reinterpret_cast<void*>(&InputRecord1<16>);
    g_inputVtable[67] = reinterpret_cast<void*>(&InputFlags);
    g_inputVtable[85] = reinterpret_cast<void*>(&InputCursor);
    g_inputVtable[90] = reinterpret_cast<void*>(&InputRecord0<90>);
    g_inputVtable[98] = reinterpret_cast<void*>(&InputRecord0<98>);
    g_inputVtable[121] = reinterpret_cast<void*>(&InputInside);
    g_inputVtable[145] = reinterpret_cast<void*>(&InputPost);
    g_serviceVtable[16] = reinterpret_cast<void*>(&ServiceGo);
    const StandKind kinds[kInputStands] = {kStandBar,        kStandBar,     kStandBarWindow, kStandBarWindow,
                                           kStandBarPicture, kStandBarPicture, kStandFactory, kStandOther,
                                           kStandCapture,    kStandCanvas,  kStandOther,     kStandOther};
    for (int i = 0; i < kInputStands; ++i)
        g_inputStands[i] = Stand{g_recordTable, nullptr, kinds[i], static_cast<uint32_t>(i % 2), 0, 0, {}};
    g_inputFactory.id = 0;
    g_inputOther.id = 0;
    g_inputCapture.id = 0;
    g_inputCanvas.id = 0;
    for (int i = 0; i < 2; ++i) {
        int32_t* const area = g_inputBarPictures[i].area;
        area[0] = 1 + i;
        area[1] = 2;
        area[2] = 13 + i;
        area[3] = 40;
    }
}

constexpr uint32_t kFactoryGlobal = 0x00B628C4; // 0x00913C72's singleton

uint32_t g_factoryWas = 0;
// The fields an item sets, put back before the window is released.
constexpr std::size_t kInputFields[] = {0x4, 0x48, 0x68, 0xE0, 0xE8, 0x164, 0x168};
uint32_t g_inputFieldsWere[std::size(kInputFields)] = {};

// A window for an item: h1a's, with the recording vtable, the picture, the
// area and the layout context's window; the stand-ins' state cleared and
// the factory the stand-in.
char* NewInputWindow(uint32_t bars = 3)
{
    char* const window = NewTestWindow();
    At<void**>(window, 0x0) = g_inputVtable;
    for (std::size_t i = 0; i < std::size(kInputFields); ++i)
        g_inputFieldsWere[i] = At<uint32_t>(window, kInputFields[i]);
    At<char*>(window, 0xE0) = window;
    At<void*>(window, 0x68) = &g_inputCanvas;
    for (std::size_t at : {std::size_t{0x24}, std::size_t{0xA8}}) {
        At<int32_t>(window, at) = 0;
        At<int32_t>(window, at + 4) = 0;
        At<int32_t>(window, at + 8) = 320;
        At<int32_t>(window, at + 0xC) = 200;
    }
    g_windowFlags = 0;
    g_inside = 1;
    g_postTaken = 0;
    g_serviceGoes = 1;
    g_factoryBars = bars;
    g_factoryMade = 0;
    g_canvasRefuses = 0x8010;
    g_captured = nullptr;
    for (int i = 0; i < 2; ++i)
        g_inputBars[i].width = 0;
    g_factoryWas = Static(kFactoryGlobal);
    Static(kFactoryGlobal) = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&g_inputFactory));
    return window;
}

// A laid-out tree by its nodes' classes, flags and boxes (the document's
// hash follows pointers that a layout leaves into the heap).
void HashBoxes(Fnv& f, char* node, int depth)
{
    f.Byte(0xA6);
    if (!node || depth > 64) {
        f.Byte(0);
        return;
    }
    f.Word(At<uint32_t>(node, 0));
    f.Byte(At<uint8_t>(node, 0x8));
    f.Byte(At<uint8_t>(node, 0x9));
    for (std::size_t at = 0xC; at < 0x1C; at += 4)
        f.Word(At<uint32_t>(node, at));
    const NodeClass* const c = ClassOf(At<uint32_t>(node, 0));
    if (!c || !c->container)
        return;
    char** const first = At<char**>(node, 0x1C);
    char** const last = At<char**>(node, 0x20);
    const uint32_t count = first && first <= last ? static_cast<uint32_t>(last - first) : 0;
    f.Word(count);
    for (uint32_t i = 0; i < count && i < 100000; ++i)
        HashBoxes(f, first[i], depth + 1);
}

// h1a's hash of the window, its document by HashBoxes.
void HashInputWindow(Fnv& f, char* window)
{
    char* const doc = At<char*>(window, 0x148);
    At<char*>(window, 0x148) = nullptr;
    HashWindow(f, window);
    At<char*>(window, 0x148) = doc;
    f.Byte(doc != nullptr);
    if (doc) {
        f.Byte(At<char*>(window, 0xE4) == doc);
        HashBoxes(f, At<char*>(doc, 0x14), 0);
    }
}

// The window hashed, its stand-ins taken off and it released as h1a's.
uint32_t FinishInputWindow(Fnv& f, char* window)
{
    HashInputWindow(f, window);
    for (std::size_t at : {std::size_t{0x4}, std::size_t{0x48}, std::size_t{0x164}, std::size_t{0x168}})
        f.Word(InputWord(window, At<uint32_t>(window, at)));
    for (int i = 0; i < 2; ++i)
        f.Word(static_cast<uint32_t>(g_inputBars[i].width));
    f.Word(g_winRecord.h);
    f.Word(g_inputRecord.h);
    Static(kFactoryGlobal) = g_factoryWas;
    At<void**>(window, 0x0) = g_winVtable;
    for (std::size_t i = 0; i < std::size(kInputFields); ++i)
        At<uint32_t>(window, kInputFields[i]) = g_inputFieldsWere[i];
    for (std::size_t at = 0x1A4; at < 0x1B4; at += 4)
        At<uint32_t>(window, at) = 0;
    g_windowFlags = 0;
    FreeTestWindow(window);
    return f.h;
}

// The client rectangle made (as slot 4 does) and a page shown through slot
// 22; then laid out through slot 10 when asked.
uint8_t ShowInputPage(char* window, const char* page, bool layOut)
{
    CallGameThis<uint32_t>(addr::HTMLWindow_UpdateClientRect, window);
    const uint8_t shown = ShowPage(window, page, "http://maxis.com/pages/");
    if (layOut) {
        int32_t size[2] = {};
        Virtual<int32_t*>(window + 0xD8, 0x28, size);
    }
    return shown;
}

const char* const kInputShort = "<HTML><BODY><A HREF=\"go.html\">go there</A> and <A NAME=\"top\">a name</A></BODY></HTML>";
// Taller and wider than the view (a long PRE line, 30 paragraphs), with two
// anchors.
const std::string kInputTallText = [] {
    std::string page = "<HTML><BODY><PRE>";
    for (int i = 0; i < 12; ++i)
        page += "wide wide ";
    page += "</PRE>";
    for (int i = 0; i < 30; ++i) {
        page += "<P>row " + std::to_string(i);
        if (i == 14)
            page += " <A NAME=\"mid\">middle</A>";
        page += "</P>";
    }
    page += "<P><A HREF=\"far.html\" NAME=\"end\">the end</A></P></BODY></HTML>";
    return page;
}();
const char* const kInputTall = kInputTallText.c_str();
const char* const kInputPaintPages[] = {
    "<HTML><BODY></BODY></HTML>",
    "<HTML><BODY BGCOLOR=#336699></BODY></HTML>",
    "<HTML><BODY BGCOLOR=#112233><TABLE BGCOLOR=#445566><TR><TD BGCOLOR=#778899></TD><TD></TD></TR></TABLE></BODY></HTML>",
};

// The view (+0xFC to +0x108) and the client rectangle.
void HashView(Fnv& f, char* window)
{
    for (std::size_t at = 0xEC; at < 0x10C; at += 4)
        f.Word(At<uint32_t>(window, at));
}

// Items, by kind.
constexpr int kInClientItems = 4;
constexpr int kInAreaItems = 4;
constexpr int kInMarginItems = 2;
constexpr int kInSizeItems = 3;
constexpr int kInPaintItems = 9;
constexpr int kInScrollItems = 6;
constexpr int kInAnchorItems = 6;
constexpr int kInPictureItems = 6;
constexpr int kInMessageItems = 5;
constexpr int kInMakeItems = 6;
constexpr int kInCheckItems = 5;
constexpr int kInRangeItems = 4;
constexpr int kInPlaceItems = 2;
constexpr int kInKeyItems = 19;
constexpr int kInWheelItems = 6;
constexpr int kInMouseItems = 10;
constexpr int kInLinkItems = 3;
constexpr int kInServiceItems = 4;
constexpr int kInGoItems = 11;
constexpr int kInputItems = kInClientItems + kInAreaItems + kInMarginItems + kInSizeItems + kInPaintItems +
                            kInScrollItems + kInAnchorItems + kInPictureItems + kInMessageItems + kInMakeItems +
                            kInCheckItems + kInRangeItems + kInPlaceItems + kInKeyItems + kInWheelItems +
                            kInMouseItems + kInLinkItems + kInServiceItems + kInGoItems;

// The keys: key, modifiers, flag 0x10, the view's top and left before (-1
// as the page leaves them), the parent taking it.
struct KeyCase {
    uint32_t key;
    uint32_t modifiers;
    bool scrolls;
    int32_t top;
    int32_t left;
    bool taken;
};
const KeyCase kInputKeys[] = {
    {0x22, 0, true, -1, -1, true},  {0x22, 0, true, -1, -1, false}, {0x22, 0, true, 980, -1, false},
    {0x28, 0, true, 100, -1, false}, {0x28, 0, false, 100, -1, false}, {0x21, 0, true, 300, -1, false},
    {0x21, 0, true, 50, -1, false},  {0x21, 0, true, 0, -1, false},  {0x26, 0, true, 7, -1, false},
    {0x24, 0, true, 120, 33, false}, {0x24, 0, true, 0, 0, false},   {0x23, 0, true, 10, 5, false},
    {0x25, 0, true, -1, 10, false},  {0x25, 0, true, -1, 300, false}, {0x27, 0, true, -1, 0, false},
    {0x27, 0, true, -1, 600, false}, {0x52, 2, true, -1, -1, false}, {0x52, 3, true, -1, -1, false},
    {0x41, 0, true, -1, -1, false},
};
static_assert(std::size(kInputKeys) == kInKeyItems);

uint32_t InputItem(int which)
{
    Fnv f;
    g_winRecord = Fnv{};
    g_inputRecord = Fnv{};
    g_paintHash = &g_inputRecord;
    const auto finish = [&](char* window) {
        const uint32_t h = FinishInputWindow(f, window);
        g_paintHash = nullptr;
        return h;
    };
    const auto margins = [](char* window, int32_t l, int32_t t, int32_t r, int32_t b) {
        At<int32_t>(window, 0x184) = l;
        At<int32_t>(window, 0x188) = t;
        At<int32_t>(window, 0x18C) = r;
        At<int32_t>(window, 0x190) = b;
    };
    if (which < kInClientItems) {
        // The client rectangle from the area and four sets of margins,
        // twice (the second unchanged).
        char* const window = NewInputWindow();
        const int32_t m[][4] = {{4, 4, 0x16, 4}, {0, 0, 0, 0}, {10, 20, 30, 40}, {-3, 5, 400, -7}};
        margins(window, m[which][0], m[which][1], m[which][2], m[which][3]);
        At<int32_t>(window, 0xFC) = which * 7;
        At<int32_t>(window, 0x100) = which * 11;
        f.Word(CallGameThis<uint32_t>(addr::HTMLWindow_UpdateClientRect, window));
        f.Word(CallGameThis<uint32_t>(addr::HTMLWindow_UpdateClientRect, window));
        HashView(f, window);
        return finish(window);
    }
    which -= kInClientItems;
    if (which < kInAreaItems) {
        // Slot 55: a new size with no page, with a page, the same size (no
        // +0x48 and with one).
        char* const window = NewInputWindow();
        if (which == 1)
            f.Byte(ShowInputPage(window, kInputShort, true));
        if (which == 3)
            At<void*>(window, 0x48) = &g_inputOther;
        const bool same = which >= 2;
        f.Word(WindowWord(window, Virtual<uint32_t>(window, 0xDC, 5, 6, same ? 325 : 400, same ? 206 : 260)));
        HashView(f, window);
        return finish(window);
    }
    which -= kInAreaItems;
    if (which < kInMarginItems) {
        // Slot 31 after slot 32.
        char* const window = NewInputWindow();
        if (which)
            Virtual<uint32_t>(window + 0xD8, 0x80, 7, -8, 9, 0x12345);
        int32_t out[4] = {0x55, 0x55, 0x55, 0x55};
        f.Word(Virtual<uint32_t>(window + 0xD8, 0x7C, &out[0], &out[1], &out[2], &out[3]));
        for (const int32_t v : out)
            f.Word(static_cast<uint32_t>(v));
        return finish(window);
    }
    which -= kInMarginItems;
    if (which < kInSizeItems) {
        // Slot 10 on a page not laid out (twice: the second laid out), on a
        // tall one, on an empty body.
        char* const window = NewInputWindow();
        f.Byte(ShowInputPage(window, which == 0 ? kInputShort : which == 1 ? kInputTall : kInputPaintPages[0], false));
        for (int pass = 0; pass < 2; ++pass) {
            int32_t size[2] = {0x77, 0x77};
            f.Word(WindowWord(window, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(
                                          Virtual<int32_t*>(window + 0xD8, 0x28, size))) -
                                          static_cast<uint32_t>(reinterpret_cast<uintptr_t>(size))));
            f.Word(static_cast<uint32_t>(size[0]));
            f.Word(static_cast<uint32_t>(size[1]));
        }
        HashView(f, window);
        return finish(window);
    }
    which -= kInSizeItems;
    if (which < kInPaintItems) {
        // Slot 88: no page shown; shown with no body's pages (the window's
        // colour, its lock refused); the three pages (BGCOLOR, a table)
        // laid out by the paint and again; with the frame; a BACKGROUND
        // picture (its lock refused); the margins changed since.
        char* const window = NewInputWindow();
        switch (which) {
        case 0:
            break;
        case 1:
        case 2:
            At<uint32_t>(window, 0x464) = 2;
            if (which == 2)
                g_canvasRefuses = 0x8001;
            break;
        case 3:
        case 4:
        case 5:
            f.Byte(ShowInputPage(window, kInputPaintPages[which - 3], false));
            break;
        case 6:
            f.Byte(ShowInputPage(window, kInputPaintPages[1], true));
            At<uint32_t>(window, 0x180) |= 0x40;
            At<uint32_t>(window, 0x1D4) = 0x00ABCDEF;
            break;
        case 7:
            f.Byte(ShowInputPage(window, kInputPaintPages[0], true));
            if (char* const body = Virtual<char*>(At<char*>(window, 0x148), 0x30))
                At<void*>(body, 0x3C) = &g_inputBarPictures[0];
            break;
        default:
            f.Byte(ShowInputPage(window, kInputPaintPages[2], true));
            margins(window, 1, 2, 3, 4);
            At<uint32_t>(window, 0x180) &= ~0x80u;
            break;
        }
        // The text runs' fonts the paint test's stand-ins (DrawString
        // recorded, nothing drawn), put back after.
        std::vector<char*> nodes;
        std::vector<std::pair<char*, char*>> fonts;
        if (char* const doc = At<char*>(window, 0x148))
            CollectNodes(At<char*>(doc, 0x14), nodes, 0);
        g_fontCount = 0;
        for (char* node : nodes)
            if (At<uint32_t>(node, 0) == 0x00ACE300)
                if (char* const real = At<char*>(node, 0x34))
                    if (Stand* const s = FontStand(real)) {
                        fonts.emplace_back(node, real);
                        At<Stand*>(node, 0x34) = s;
                    }
        f.Word(static_cast<uint32_t>(fonts.size()));
        f.Byte(Virtual<uint8_t>(window, 0x160));
        f.Byte(Virtual<uint8_t>(window, 0x160));
        for (const auto& [node, real] : fonts)
            At<char*>(node, 0x34) = real;
        if (which == 7)
            if (char* const body = Virtual<char*>(At<char*>(window, 0x148), 0x30))
                At<void*>(body, 0x3C) = nullptr;
        HashView(f, window);
        return finish(window);
    }
    which -= kInPaintItems;
    if (which < kInScrollItems) {
        // Slot 7 on the tall page: to where it is, inside, below and right
        // of the page, before it, with no bars.
        char* const window = NewInputWindow(which == 5 ? 0 : 3);
        f.Byte(ShowInputPage(window, kInputTall, true));
        const int32_t to[][2] = {{0, 0}, {30, 200}, {5000, 5000}, {400, 10}, {-20, -30}, {10, 10}};
        f.Word(Virtual<uint32_t>(window + 0xD8, 0x1C, to[which][0], to[which][1]));
        HashView(f, window);
        return finish(window);
    }
    which -= kInScrollItems;
    if (which < kInAnchorItems) {
        // Slots 8 and 9 with three names on the tall page (one not there),
        // one above the view, one on a page not laid out.
        char* const window = NewInputWindow();
        f.Byte(ShowInputPage(window, kInputTall, which != 5));
        // 3: mid; 4: mid after the view went below it; 5: end, not laid out.
        const char* const names[] = {"mid", "end", "nowhere", "mid", "mid", "end"};
        GameText name;
        MakeText(&name, names[which]);
        if (which < 3) {
            int32_t rect[4] = {0x66, 0x66, 0x66, 0x66};
            f.Byte(Virtual<uint8_t>(window + 0xD8, 0x20, &name, rect));
            for (const int32_t v : rect)
                f.Word(static_cast<uint32_t>(v));
        } else {
            if (which == 4)
                Virtual<uint32_t>(window + 0xD8, 0x1C, 0, 900);
            f.Byte(Virtual<uint8_t>(window + 0xD8, 0x24, &name));
        }
        DestroyText(&name);
        HashView(f, window);
        return finish(window);
    }
    which -= kInAnchorItems;
    if (which < kInPictureItems) {
        // Slots 11 and 12 (with and without the bar, an empty part and not,
        // none), then 13 and 14.
        char* const window = NewInputWindow();
        if (which >= 2)
            f.Byte(ShowInputPage(window, kInputTall, true));
        const int32_t parts[][4] = {{0, 0, 0, 0}, {3, 4, 15, 60}};
        void* const picture = which == 4 ? nullptr : &g_inputBarPictures[which % 2];
        const std::size_t slot = which % 2 ? 0x30 : 0x2C;
        f.Byte(Virtual<uint8_t>(window + 0xD8, slot, picture, parts[(which / 2) % 2]));
        if (which == 5)
            f.Byte(Virtual<uint8_t>(window + 0xD8, slot, &g_inputBarPictures[0], parts[0]));
        for (const std::size_t get : {std::size_t{0x34}, std::size_t{0x38}}) {
            void* got = reinterpret_cast<void*>(0x99);
            int32_t rect[4] = {0x44, 0x44, 0x44, 0x44};
            const uint32_t r = Virtual<uint32_t>(window + 0xD8, get, &got, rect);
            f.Byte(static_cast<uint8_t>(r));
            f.Word(InputWord(window, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(got))));
            for (const int32_t v : rect)
                f.Word(static_cast<uint32_t>(v));
        }
        return finish(window);
    }
    which -= kInPictureItems;
    if (which < kInMessageItems) {
        // Slot 143: another message, from the vertical bar, from the
        // horizontal one, from neither, and from the horizontal with no
        // vertical bar.
        char* const window = NewInputWindow();
        f.Byte(ShowInputPage(window, kInputTall, true));
        At<uint32_t>(window, 0x180) |= 4;
        CallGameThis<uint8_t>(addr::HTMLWindow_MakeScrollBars, window, uint8_t{1}, uint8_t{1});
        g_inputBars[0].width = 120;
        g_inputBars[1].width = 45;
        if (which == 4)
            At<void*>(window, 0x1A4) = nullptr;
        const uint32_t ids[] = {0x6300, 0x6300, 0x6301, 0x6399, 0x6301};
        f.Byte(Virtual<uint8_t>(window, 0x23C, which == 0 ? 0x12345678u : 0x887113A3u, ids[which]));
        HashView(f, window);
        return finish(window);
    }
    which -= kInMessageItems;
    if (which < kInMakeItems) {
        // The bars made: both (the horizontal allowed), the vertical only,
        // the factory giving none, with pictures and parts set before, with
        // the flags off, and a bar already there.
        char* const window = NewInputWindow(which == 2 ? 0 : 3);
        f.Byte(ShowInputPage(window, kInputShort, true));
        if (which != 1)
            At<uint32_t>(window, 0x180) |= 4;
        if (which == 3) {
            const int32_t part[4] = {1, 1, 9, 30};
            const int32_t none[4] = {};
            Virtual<uint8_t>(window + 0xD8, 0x2C, &g_inputBarPictures[0], part);
            Virtual<uint8_t>(window + 0xD8, 0x30, &g_inputBarPictures[1], none);
        }
        if (which == 4)
            At<uint32_t>(window, 0x180) &= ~0xCu;
        if (which == 5)
            At<void*>(window, 0x1A4) = &g_inputBars[1];
        f.Byte(CallGameThis<uint8_t>(addr::HTMLWindow_MakeScrollBars, window, uint8_t{1}, uint8_t(which != 1)));
        HashView(f, window);
        return finish(window);
    }
    which -= kInMakeItems;
    if (which < kInCheckItems) {
        // The bars checked: the tall page (made), the short one with bars
        // (dropped), the tall one with the horizontal bar allowed, the
        // short one with it allowed, and the view made exactly the page's
        // height and width with both bars (dropped).
        char* const window = NewInputWindow();
        f.Byte(ShowInputPage(window, which % 2 ? kInputShort : kInputTall, false));
        if (which >= 2)
            At<uint32_t>(window, 0x180) |= 4;
        if (which % 2) {
            At<void*>(window, 0x1A4) = &g_inputBars[0];
            At<void*>(window, 0x1A8) = &g_inputBars[1];
        }
        int32_t size[2] = {};
        Virtual<int32_t*>(window + 0xD8, 0x28, size);
        if (which == 4) {
            At<int32_t>(window, 0x104) = At<int32_t>(window, 0xFC) + size[0];
            At<int32_t>(window, 0x108) = At<int32_t>(window, 0x100) + size[1];
        }
        f.Byte(CallGameThis<uint8_t>(addr::HTMLWindow_UpdateScrollBars, window));
        HashView(f, window);
        return finish(window);
    }
    which -= kInCheckItems;
    if (which < kInRangeItems) {
        // The ranges and positions, with and without ranges, the vertical
        // bar alone and both (the horizontal ones go to the vertical bar).
        char* const window = NewInputWindow();
        f.Byte(ShowInputPage(window, kInputTall, true));
        At<int32_t>(window, 0xFC) = 11;
        At<int32_t>(window, 0x100) = 77;
        if (which >= 2)
            At<void*>(window, 0x1A8) = &g_inputBars[1];
        if (!At<void*>(window, 0x1A4))
            At<void*>(window, 0x1A4) = &g_inputBars[0];
        CallGameThis<void>(addr::HTMLWindow_SetScrollRanges, window, uint8_t(which % 2));
        return finish(window);
    }
    which -= kInRangeItems;
    if (which < kInPlaceItems) {
        // The bars placed: the vertical one, both.
        char* const window = NewInputWindow();
        At<void*>(window, 0x1A4) = &g_inputBars[0];
        if (which)
            At<void*>(window, 0x1A8) = &g_inputBars[1];
        At<int32_t>(window, 0xB0) = 333;
        CallGameThis<void>(addr::HTMLWindow_PlaceScrollBars, window);
        return finish(window);
    }
    which -= kInPlaceItems;
    if (which < kInKeyItems) {
        // Slot 130, each key on the tall page.
        const KeyCase& c = kInputKeys[which];
        char* const window = NewInputWindow();
        f.Byte(ShowInputPage(window, kInputTall, true));
        if (c.scrolls)
            At<uint32_t>(window, 0x180) |= 0x10;
        else
            At<uint32_t>(window, 0x180) &= ~0x10u;
        const auto move = [&](std::size_t at, int32_t to) {
            if (to < 0)
                return;
            const int32_t size = At<int32_t>(window, at + 8) - At<int32_t>(window, at);
            At<int32_t>(window, at) = to;
            At<int32_t>(window, at + 8) = to + size;
        };
        move(0xFC, c.left);
        move(0x100, c.top);
        if (c.key == 0x52)
            SetUrl(window + 0x2E0, "http://maxis.com/pages/shown.html");
        g_postTaken = c.taken ? 1 : 0;
        f.Byte(Virtual<uint8_t>(window, 0x208, c.key, c.modifiers));
        HashView(f, window);
        return finish(window);
    }
    which -= kInKeyItems;
    if (which < kInWheelItems) {
        // Slot 139: the mouse captured; up 250 and down 150 with flag 0x10
        // and without; 50; no page.
        char* const window = NewInputWindow();
        if (which != 5)
            f.Byte(ShowInputPage(window, kInputTall, true));
        At<void*>(window, 0x4) = &g_inputCapture;
        if (which == 0)
            g_captured = window;
        if (which == 1 || which == 3)
            At<uint32_t>(window, 0x180) |= 0x10;
        else
            At<uint32_t>(window, 0x180) &= ~0x10u;
        At<int32_t>(window, 0x100) = 400;
        At<int32_t>(window, 0x108) = 600;
        const int32_t deltas[] = {250, 250, 250, -150, 50, -300};
        f.Byte(Virtual<uint8_t>(window, 0x22C, 1u, 2u, 3u, deltas[which]));
        HashView(f, window);
        return finish(window);
    }
    which -= kInWheelItems;
    if (which < kInMouseItems) {
        // Slots 134, 136 and 138 over the link, over the image that is no
        // link, outside the window, with flag 0x20 off; the node hit alone
        // on a page not laid out and with no page.
        char* const window = NewInputWindow();
        if (which < 8)
            f.Byte(ShowInputPage(window, kInputShort, true));
        else if (which == 8)
            f.Byte(ShowInputPage(window, kInputShort, false));
        At<void*>(window, 0x164) = &g_inputStands[10];
        At<void*>(window, 0x168) = &g_inputStands[11];
        const int32_t points[][2] = {{10, 10}, {66, 10}, {10, 10}, {300, 150}};
        const int32_t* const p = points[which % 4];
        if (which == 2)
            g_inside = 0;
        if (which >= 4 && which < 8)
            At<uint32_t>(window, 0x180) &= ~0x20u;
        if (which < 8) {
            g_postTaken = which % 2;
            for (const std::size_t slot : {std::size_t{0x228}, std::size_t{0x218}, std::size_t{0x220}})
                f.Byte(Virtual<uint8_t>(window, slot, p[0], p[1], 0u));
        } else {
            char* node = reinterpret_cast<char*>(0x55);
            f.Byte(CallGameThis<uint8_t>(addr::HTMLWindow_HitNode, window, &node, 10, 10));
            f.Byte(node == reinterpret_cast<char*>(0x55));
        }
        return finish(window);
    }
    which -= kInMouseItems;
    if (which < kInLinkItems) {
        // Slot 25: not taken; taken with flag 0x20; taken without it.
        char* const window = NewInputWindow();
        g_postTaken = which ? 1 : 0;
        if (which == 2)
            At<uint32_t>(window, 0x180) &= ~0x20u;
        GameText link;
        MakeText(&link, "sc4://advice/7");
        f.Byte(Virtual<uint8_t>(window + 0xD8, 0x64, &link));
        DestroyText(&link);
        return finish(window);
    }
    which -= kInLinkItems;
    if (which < kInServiceItems) {
        // Slot 4: the same service, another, then from a recording one to
        // none and to another (AddRef and Release recorded).
        char* const window = NewInputWindow();
        void* const services[] = {&g_service, &g_inputStands[11], nullptr, &g_inputStands[10]};
        g_inputStands[10].kind = kStandBarPicture;
        g_inputStands[11].kind = kStandBarPicture;
        if (which >= 2)
            At<void*>(window, 0x460) = &g_inputStands[11];
        f.Byte(Virtual<uint8_t>(window + 0xD8, 0x10, services[which]));
        f.Word(InputWord(window, At<uint32_t>(window, 0x460)));
        g_inputStands[10].kind = kStandOther;
        g_inputStands[11].kind = kStandOther;
        At<void*>(window, 0x460) = &g_service;
        return finish(window);
    }
    which -= kInServiceItems;
    // Slots 17, 18 and 20: not created; no service; an empty address; the
    // page shown again (not reloading, reloading); a new address (relative
    // to the base); asked while asking; the service refusing; a fragment;
    // slot 18 from a string; an empty fragment.
    char* const window = NewInputWindow();
    if (which != 0)
        g_windowFlags = 0x4000;
    f.Byte(ShowInputPage(window, kInputShort, false));
    SetUrl(window + 0x2E0, "http://maxis.com/pages/shown.html");
    if (which == 1)
        At<void*>(window, 0x460) = nullptr;
    if (which == 6)
        At<uint32_t>(window, 0x464) = 1;
    if (which == 7)
        g_serviceGoes = 0;
    const char* const texts[] = {"x.html", "x.html", "", "http://maxis.com/pages/shown.html",
                                 "http://maxis.com/pages/shown.html", "next.html?a=1", "next.html", "other.html"};
    if (which < 8) {
        alignas(4) char url[0xC0];
        GameText text;
        MakeText(&text, texts[which]);
        reinterpret_cast<void*(__thiscall*)(void*, const void*)>(addr::SC4Url_FromText)(url, &text);
        if (which == 6)
            SetUrl(window + 0x3A0, "http://maxis.com/pages/next.html");
        f.Byte(Virtual<uint8_t>(window + 0xD8, 0x44, static_cast<void*>(url), uint32_t(which == 4 ? 1 : 0)));
        reinterpret_cast<void(__thiscall*)(void*)>(addr::SC4Url_Destroy)(url);
        DestroyText(&text);
    } else if (which == 8 || which == 10) {
        // A fragment, and none (the page shown, not reloaded).
        GameText fragment;
        MakeText(&fragment, which == 8 ? "#mid" : "");
        f.Byte(Virtual<uint8_t>(window + 0xD8, 0x50, &fragment));
        DestroyText(&fragment);
    } else {
        GameText text;
        MakeText(&text, "../up.html");
        f.Byte(Virtual<uint8_t>(window + 0xD8, 0x48, &text, 0u));
        DestroyText(&text);
    }
    if (which == 1)
        At<void*>(window, 0x460) = &g_service;
    return finish(window);
}

// ====================================================================================
// h1c: the directors and the shared block's release

// A director's list (+0x2C to +0x30, its room to +0x34) and its classes
// (buckets +0x3C to +0x40, nodes {next, class, factory, type}, the count at
// +0x48): the list's length, entries and room, the factory and type of the
// window's class and of one not registered, the count. Heap pointers are not
// hashed.
void HashDirector(Fnv& f, char* director)
{
    auto* const first = At<uint32_t*>(director, 0x2C);
    auto* const last = At<uint32_t*>(director, 0x30);
    f.Word(first && last >= first ? static_cast<uint32_t>(last - first) : 0xFFFFFFFFu);
    for (uint32_t* at = first; at && at != last; ++at)
        f.Word(*at);
    auto* const buckets = At<char**>(director, 0x3C);
    const auto count = static_cast<uint32_t>(At<char**>(director, 0x40) - buckets);
    f.Word(count);
    for (const uint32_t id : {0xAA12E5F5u, 0x12345678u}) {
        uint32_t factory = 0, type = 0;
        for (char* node = count ? buckets[id % count] : nullptr; node; node = At<char*>(node, 0))
            if (At<uint32_t>(node, 4) == id) {
                factory = At<uint32_t>(node, 8);
                type = At<uint32_t>(node, 0xC);
                break;
            }
        f.Word(factory);
        f.Word(type);
    }
    f.Word(At<uint32_t>(director, 0x8));
    f.Word(At<uint32_t>(director, 0xC));
    f.Byte(At<uint32_t>(director, 0x24) != 0 ? 1 : 0); // the COM and what it gave (heap pointers)
    f.Byte(At<uint32_t>(director, 0x28) != 0 ? 1 : 0);
    f.Word(At<uint32_t>(director, 0x34) - At<uint32_t>(director, 0x2C)); // the list's room
    f.Word(At<uint32_t>(director, 0x48));                                // the classes registered
}

// The release's cases: the count at +8, the word above it (+0xA), whether
// the block has storage.
struct BlockCase {
    uint16_t count;
    uint16_t above;
    bool storage;
};
const BlockCase kBlockCases[] = {{3, 0x1234, true}, {2, 0, true},      {0xFFFF, 0xBEEF, true}, {0x100, 7, true},
                                 {1, 0x1234, true}, {0, 0xBEEF, true}, {1, 0, false},          {0, 0, false}};
constexpr int kDirectorItems = 6 + static_cast<int>(std::size(kBlockCases));

uint32_t DirectorItem(int which)
{
    using ThisFn = char*(__thiscall*)(void*);
    using IdFn = uint32_t(__thiscall*)(void*);
    using GetFn = char*(__cdecl*)();
    Fnv f;
    if (which == 0 || which == 1) {
        // A constructor on a director of the test's own (patterned around
        // it), then the base's destructor. Its COM (+0x24) is none, so
        // adding the second director leaves the static one alone.
        alignas(8) char space[16 + 0x4C + 16];
        std::memset(space, 0xA5, sizeof space);
        char* const self = space + 16;
        const std::uintptr_t construct = which == 0 ? addr::HTMLDirector_Construct : addr::HTMLSubDirector_Construct;
        f.Byte(reinterpret_cast<ThisFn>(construct)(self) == self ? 1 : 0);
        f.Word(At<uint32_t>(self, 0));
        f.Word(At<uint32_t>(self, 4));
        f.Bytes(space, space + 16);
        f.Bytes(space + 16 + 0x4C, space + sizeof space);
        HashDirector(f, self);
        f.Word(Virtual<uint32_t>(self, 0x34));
        reinterpret_cast<ThisFn>(addr::cRZCOMDllDirector_Destruct)(self);
        return f.h;
    }
    if (which == 2 || which == 3) {
        // The IDs by address (any `this`) and through the static directors'
        // vtables.
        const std::uintptr_t id = which == 2 ? addr::HTMLDirector_GetID : addr::HTMLSubDirector_GetID;
        f.Word(reinterpret_cast<IdFn>(id)(nullptr));
        f.Word(reinterpret_cast<IdFn>(id)(reinterpret_cast<void*>(static_cast<std::uintptr_t>(0x12345678u))));
        char* const director = reinterpret_cast<char*>(which == 2 ? 0x00B623F0u : 0x00B62440u);
        f.Word(Virtual<uint32_t>(director, 0x34));
        return f.h;
    }
    if (which == 4 || which == 5) {
        // The getters (the directors are made, so each is handed back), the
        // guard words and the static HTML director's list and classes.
        const std::uintptr_t get = which == 4 ? addr::HTMLDirector_Get : addr::HTMLSubDirector_Get;
        char* const director = reinterpret_cast<GetFn>(get)();
        f.Word(static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(director)));
        f.Word(*reinterpret_cast<const uint32_t*>(which == 4 ? 0x00B6243Cu : 0x00B6248Cu));
        f.Word(At<uint32_t>(director, 0));
        f.Word(At<uint32_t>(director, 4));
        if (which == 4)
            HashDirector(f, director);
        return f.h;
    }
    // The release on blocks of the test's own (operator new, as the game's
    // blocks are): the count taken down, or the storage and block freed.
    const BlockCase& c = kBlockCases[which - 6];
    Blocks blocks;
    char* const block = reinterpret_cast<char*>(static_cast<std::uintptr_t>(blocks.Add(0xC, true)));
    void* storage = nullptr;
    if (c.storage)
        storage = reinterpret_cast<void*>(static_cast<std::uintptr_t>(blocks.Add(0x30, true)));
    At<void*>(block, 0) = storage;
    At<uint32_t>(block, 4) = 0x5A5A5A5Au;
    At<uint16_t>(block, 8) = c.count;
    At<uint16_t>(block, 0xA) = c.above;
    const uint32_t left = reinterpret_cast<uint32_t(__thiscall*)(void*)>(addr::HTMLSharedBlock_Release)(block);
    f.Word(left);
    if (c.count > 1) {
        f.Byte(At<void*>(block, 0) == storage ? 1 : 0);
        f.Word(At<uint32_t>(block, 4));
        f.Word(At<uint32_t>(block, 8));
        if (storage)
            GameDelete(storage);
        GameDelete(block);
        return f.h;
    }
    HashFreedDeep(f, blocks);
    return f.h;
}

// ====================================================================================
// h7a: the file cache's entries

// A file cache of the test's own (0x5C bytes, its constructor 0x008FD389;
// its folder empty, never opened, so nothing reads or writes FileCache.ini)
// with entries of five kinds, by index modulo 5: fresh with a file name;
// expired, no name; fresh with a data block (a count of 1, made by
// operator new as the game's are) and a name; expired by the signed clock
// but not by the unsigned one (0x80000001), no name; fresh with nothing.
// Only nameless entries can expire, so the drops' file deletes
// (0x008FC669) have no file to delete. The names are of no file there is.
constexpr std::uintptr_t kFileCacheDestruct = 0x008FD729;
constexpr int kCacheKinds = 5;

struct CacheEntry {
    GameText name;
    uint32_t block;
    uint32_t size;
    uint32_t words[3];
    uint32_t used;
    uint32_t expiry;
};
static_assert(sizeof(CacheEntry) == 0x30);

std::string CacheKey(int i)
{
    char text[64];
    if (i % 7 == 6)
        std::snprintf(text, sizeof text, "http://www.simcity.com/\xE9t\xE9/%d", i);
    else
        std::snprintf(text, sizeof text, "http://www.simcity.com/page%d.html", i);
    return text;
}

char* MakeCache()
{
    char* const cache = static_cast<char*>(GameNew(0x5C));
    std::memset(cache, 0xA5, 0x5C);
    reinterpret_cast<char*(__thiscall*)(void*)>(addr::FileCache_Construct)(cache);
    return cache;
}

// The data blocks released (ReleaseAll), found freed while they are the
// pool's last frees, then the cache destroyed.
void DestroyCache(Fnv& f, char* cache, const Blocks& blocks)
{
    reinterpret_cast<void(__thiscall*)(void*)>(addr::FileCache_ReleaseAll)(cache);
    HashFreedDeep(f, blocks);
    reinterpret_cast<void(__thiscall*)(void*)>(kFileCacheDestruct)(cache);
    GameDelete(cache);
}

// An entry put in through the game's addresses as the ini reader does: a
// pair made (0x008FCB5D), inserted (0x008FD705), the pair and strings
// destroyed. Answers the inserted byte.
uint8_t AddEntry(char* cache, int i, Blocks& blocks, bool again = false)
{
    const int kind = i % kCacheKinds;
    GameText key{};
    MakeText(&key, CacheKey(i).c_str());
    CacheEntry entry{};
    char name[64] = "";
    if (kind == 0 || kind == 2)
        std::snprintf(name, sizeof name, "opensc4-h7a-none-%d.cache", i);
    MakeText(&entry.name, name);
    if (kind == 2) {
        char* const block = reinterpret_cast<char*>(static_cast<std::uintptr_t>(blocks.Add(0xC, true)));
        At<uint32_t>(block, 0) = blocks.Add(0x10, true);
        At<uint32_t>(block, 4) = 0x10;
        At<uint16_t>(block, 8) = 1;
        At<uint16_t>(block, 0xA) = 0;
        entry.block = static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(block));
    }
    entry.size = 0x100u + static_cast<uint32_t>(i);
    entry.words[0] = 0x1000u * static_cast<uint32_t>(i);
    entry.words[1] = again ? 0xDEADu : 0x2u;
    entry.words[2] = 0x30000u + static_cast<uint32_t>(i);
    entry.used = 0x12345u;
    entry.expiry = kind == 1 ? 1u : kind == 3 ? 0x80000001u : 0x7FFFFFFFu;
    alignas(4) char pair[0x44];
    reinterpret_cast<void*(__thiscall*)(void*, const void*, const void*)>(addr::HTMLCacheMap_MakePair)(pair, &key,
                                                                                                    &entry);
    alignas(4) char out[12] = {};
    reinterpret_cast<void*(__thiscall*)(void*, void*, const void*)>(addr::HTMLCacheMap_Insert)(cache + 0x34, out,
                                                                                            pair);
    reinterpret_cast<void(__thiscall*)(void*)>(addr::SC4StringPair_Destruct)(pair);
    DestroyText(&entry.name);
    DestroyText(&key);
    return static_cast<uint8_t>(out[8]);
}

void HashGameText(Fnv& f, const char* text) { f.Bytes(At<char*>(const_cast<char*>(text), 4), At<char*>(const_cast<char*>(text), 8)); }

// An entry: its name, its block (its index among the test's blocks), its
// words; last used (+0x28) as a marker when it is the clock (slot 16 sets
// it), which differs from run to run.
void HashCacheEntry(Fnv& f, const char* entry, const Blocks& blocks)
{
    HashGameText(f, entry);
    const uint32_t block = At<uint32_t>(const_cast<char*>(entry), 0x14);
    f.Word(block ? static_cast<uint32_t>(blocks.IndexOf(block)) : 0xFFFFFFFFu);
    if (block)
        f.Word(At<uint16_t>(reinterpret_cast<char*>(static_cast<std::uintptr_t>(block)), 8));
    const uint32_t now = static_cast<uint32_t>(reinterpret_cast<int32_t(__cdecl*)(void*)>(addr::CRT_time32)(nullptr));
    for (std::size_t at = 0x18; at <= 0x2C; at += 4) {
        const uint32_t word = At<uint32_t>(const_cast<char*>(entry), at);
        f.Word(at == 0x28 && word <= now && now - word < 60 ? 0xC10C4u : word);
    }
}

// The map bucket by bucket (each node's address and entry, in chain
// order), the count, the cache's two request counters.
void HashCacheMap(Fnv& f, char* cache, const Blocks& blocks)
{
    char* const map = cache + 0x34;
    char** const first = At<char**>(map, 4);
    char** const last = At<char**>(map, 8);
    const uint32_t buckets = static_cast<uint32_t>(last - first);
    f.Word(buckets);
    f.Word(At<uint32_t>(map, 0x10));
    uint32_t seen = 0;
    for (uint32_t b = 0; b < buckets; ++b) {
        uint32_t chain = 0;
        for (char* node = first[b]; node && chain < 10000; node = At<char*>(node, 0), ++chain) {
            f.Word(b);
            HashGameText(f, node + 4);
            HashCacheEntry(f, node + 0x18, blocks);
        }
        seen += chain;
    }
    f.Word(seen);
    f.Word(At<uint32_t>(cache, 0x54));
    f.Word(At<uint32_t>(cache, 0x58));
}

// A stand-in for Release's slot 0: the flag and the object recorded.
uint32_t g_releaseFlag = 0;
void* g_releaseSelf = nullptr;
void* __fastcall StandReleaseDelete(void* self, void*, uint32_t flag)
{
    g_releaseFlag = flag;
    g_releaseSelf = self;
    return self;
}
void* g_releaseVtable[1] = {reinterpret_cast<void*>(&StandReleaseDelete)};

constexpr int kCacheSizes[] = {0, 1, 7, 250};
constexpr uint32_t kReleaseCounts[] = {5, 2, 1, 0, 0xFFFFFFFFu};
constexpr int kCacheItems = 4 + 14 + static_cast<int>(std::size(kReleaseCounts));
constexpr int kCacheEntries = 23; // the entries of the items after the sizes

uint32_t CacheItem(int which)
{
    using Slot1 = uint8_t(__thiscall*)(void*, const void*);
    Fnv f;
    if (which >= 18) {
        // Release (slot 4) on an object of the test's own.
        const uint32_t count = kReleaseCounts[which - 18];
        alignas(4) char object[16];
        std::memset(object, 0x5A, sizeof object);
        At<void**>(object, 0) = g_releaseVtable;
        At<uint32_t>(object, 8) = count;
        g_releaseFlag = 0xFFu;
        g_releaseSelf = nullptr;
        f.Word(reinterpret_cast<uint32_t(__thiscall*)(void*)>(addr::FileCache_Release)(object));
        f.Word(At<uint32_t>(object, 8));
        f.Word(g_releaseFlag);
        f.Byte(g_releaseSelf == object ? 1 : 0);
        return f.h;
    }
    Blocks blocks;
    char* const cache = MakeCache();
    if (which < 4) {
        // Sizes: the map after n inserts (250 grows it), each address
        // looked up through slots 17, 20 and 21, and one not there.
        const int n = kCacheSizes[which];
        for (int i = 0; i < n; ++i)
            f.Byte(AddEntry(cache, i, blocks));
        HashCacheMap(f, cache, blocks);
        for (int i = 0; i <= n; ++i) {
            GameText key{};
            MakeText(&key, (i == n ? std::string("http://nowhere/") : CacheKey(i)).c_str());
            char* entry = nullptr;
            f.Byte(Virtual<uint8_t>(cache, 0x44, static_cast<const void*>(&key), &entry));
            f.Byte(entry ? 1 : 0);
            f.Byte(Virtual<uint8_t>(cache, 0x44, static_cast<const void*>(&key), static_cast<char**>(nullptr)));
            f.Word(Virtual<uint32_t>(cache, 0x50, static_cast<const void*>(&key)));
            f.Word(Virtual<uint32_t>(cache, 0x54, static_cast<const void*>(&key)));
            DestroyText(&key);
        }
        DestroyCache(f, cache, blocks);
        return f.h;
    }
    for (int i = 0; i < kCacheEntries; ++i)
        AddEntry(cache, i, blocks);
    auto forEachKey = [&](auto run) {
        for (int i = 0; i <= kCacheEntries; ++i) {
            GameText key{};
            MakeText(&key, (i == kCacheEntries ? std::string("http://nowhere/") : CacheKey(i)).c_str());
            run(i, &key);
            DestroyText(&key);
        }
    };
    const int step = which - 4;
    if (step == 0) {
        // Duplicates: not inserted, the first entry kept.
        for (int i = 0; i < 6; ++i)
            f.Byte(AddEntry(cache, i, blocks, true));
    } else if (step <= 3) {
        // Freshness through slots 12, 13 and 14 (the expired dropped).
        const std::size_t slot = 0x30 + 4 * static_cast<std::size_t>(step - 1);
        forEachKey([&](int, const GameText* key) { f.Byte(Virtual<uint8_t>(cache, slot, static_cast<const void*>(key))); });
        // and once more, after the drops.
        forEachKey([&](int, const GameText* key) { f.Byte(Virtual<uint8_t>(cache, slot, static_cast<const void*>(key))); });
    } else if (step == 4) {
        // GetEntry (slot 18) into an entry of the test's own.
        forEachKey([&](int, const GameText* key) {
            CacheEntry out{};
            MakeText(&out.name, "left as it was");
            for (uint32_t& w : out.words)
                w = 0xCCCCCCCCu;
            out.size = out.used = out.expiry = 0xCCCCCCCCu;
            f.Byte(Virtual<uint8_t>(cache, 0x48, static_cast<const void*>(key), static_cast<void*>(&out)));
            HashCacheEntry(f, reinterpret_cast<const char*>(&out), blocks);
            DestroyText(&out.name);
        });
    } else if (step == 5) {
        // Remove (slot 25) of the nameless and one not there, the third
        // after a purge (the requests since the last at 39).
        At<uint32_t>(cache, 0x58) = 38;
        forEachKey([&](int i, const GameText* key) {
            const int kind = i % kCacheKinds;
            if (i == kCacheEntries || kind == 1 || kind == 3 || kind == 4)
                f.Byte(Virtual<uint8_t>(cache, 0x64, static_cast<const void*>(key)));
        });
    } else if (step == 6) {
        // Purge: the unsigned expiry keeps 0x80000001.
        reinterpret_cast<void(__thiscall*)(void*)>(addr::FileCache_Purge)(cache);
    } else if (step == 7) {
        // Erase by iterators (every third address), then the map walked
        // with its iterator.
        forEachKey([&](int i, const GameText* key) {
            if (i % 3 != 0)
                return;
            char* it[2] = {};
            reinterpret_cast<void*(__thiscall*)(void*, void*, const void*)>(addr::HTMLCacheMap_Find)(cache + 0x34, it,
                                                                                                  key);
            f.Byte(it[0] ? 1 : 0);
            if (it[0] && At<uint32_t>(it[0], 0x2C))
                return; // a node with a block keeps it (Erase leaves blocks to ReleaseAll)
            reinterpret_cast<void(__thiscall*)(void*, void*)>(addr::HTMLCacheMap_Erase)(cache + 0x34, it);
        });
        char* it[2] = {};
        reinterpret_cast<void*(__thiscall*)(void*, void*)>(addr::cGZDBSegmentPackedFile_KeyTableBegin)(cache + 0x34, it);
        uint32_t walked = 0;
        while (it[0] && walked < 10000) {
            HashGameText(f, it[0] + 4);
            ++walked;
            reinterpret_cast<void*(__thiscall*)(void*)>(addr::HTMLCacheMap_Increment)(it);
        }
        f.Word(walked);
    } else if (step == 8) {
        // ReleaseAll: the blocks released, the map cleared, then filled
        // again in the same buckets.
        reinterpret_cast<void(__thiscall*)(void*)>(addr::FileCache_ReleaseAll)(cache);
        HashCacheMap(f, cache, blocks);
        for (int i = 0; i < 4; ++i)
            f.Byte(AddEntry(cache, i * 5, blocks));
    } else if (step == 9) {
        // Slot 19 (slot 16 with a string of its own) on each address.
        forEachKey([&](int, const GameText* key) {
            f.Byte(reinterpret_cast<Slot1>(addr::FileCache_AskWithString)(cache, key));
        });
    } else if (step == 10) {
        // The folder and the ini name (slots 5 and 7), and the clock.
        GameText text{};
        MakeText(&text, "left as it was");
        f.Byte(Virtual<uint8_t>(cache, 0x14, static_cast<void*>(&text)));
        HashGameText(f, reinterpret_cast<const char*>(&text));
        f.Byte(Virtual<uint8_t>(cache, 0x1C, static_cast<void*>(&text)));
        HashGameText(f, reinterpret_cast<const char*>(&text));
        DestroyText(&text);
        const int32_t clock = Virtual<int32_t>(cache, 0x58);
        const int32_t now = reinterpret_cast<int32_t(__cdecl*)(void*)>(addr::CRT_time32)(nullptr);
        f.Byte(clock == now || clock + 1 == now ? 1 : 0);
    } else if (step == 11) {
        // The hash and bucket of odd addresses, and the buckets grown by
        // hints at, below and above their count.
        char* const map = cache + 0x34;
        for (const char* s : {"", "a", "\x80", "\xFF\xFF", "http://www.simcity.com/"}) {
            GameText key{};
            MakeText(&key, s);
            f.Word(reinterpret_cast<uint32_t(__thiscall*)(void*, const void*)>(addr::HTMLCacheMap_Hash)(map, &key));
            f.Word(reinterpret_cast<uint32_t(__thiscall*)(void*, const void*)>(addr::HTMLCacheMap_BucketOf)(map, &key));
            DestroyText(&key);
        }
        const uint32_t count = static_cast<uint32_t>(At<char**>(map, 8) - At<char**>(map, 4));
        for (const uint32_t hint : {0u, count - 1, count, count + 1, 1000u, 999u}) {
            reinterpret_cast<void(__thiscall*)(void*, uint32_t)>(addr::HTMLCacheMap_Resize)(map, hint);
            HashCacheMap(f, cache, blocks);
        }
    } else if (step == 12) {
        // Clear, then the map filled again.
        reinterpret_cast<void(__thiscall*)(void*)>(addr::FileCache_ReleaseAll)(cache);
        reinterpret_cast<void(__thiscall*)(void*)>(addr::HTMLCacheMap_Clear)(cache + 0x34);
        HashCacheMap(f, cache, blocks);
        for (int i = 0; i < 3; ++i)
            f.Byte(AddEntry(cache, i, blocks));
    } else {
        // A node made and deleted by hand, and an entry copied and set.
        GameText key{};
        MakeText(&key, "http://www.simcity.com/node");
        CacheEntry entry{};
        MakeText(&entry.name, "a name");
        entry.size = 7;
        entry.expiry = 0x7FFFFFFFu;
        alignas(4) char pair[0x44];
        reinterpret_cast<void*(__thiscall*)(void*, const void*, const void*)>(addr::HTMLCacheMap_MakePair)(pair, &key,
                                                                                                        &entry);
        char* const node =
            reinterpret_cast<char*(__thiscall*)(void*, const void*)>(addr::HTMLCacheMap_NewNode)(cache + 0x34, pair);
        f.Word(At<uint32_t>(node, 0));
        HashGameText(f, node + 4);
        HashCacheEntry(f, node + 0x18, blocks);
        Blocks nodes;
        nodes.Track(node, 0x48, false);
        reinterpret_cast<void(__thiscall*)(void*, void*)>(addr::HTMLCacheMap_DeleteNode)(cache + 0x34, node);
        HashFreedDeep(f, nodes);
        alignas(4) char copy[0x30];
        std::memset(copy, 0x5A, sizeof copy);
        reinterpret_cast<void*(__thiscall*)(void*, const void*)>(addr::HTMLCacheEntry_Copy)(copy, &entry);
        HashCacheEntry(f, copy, blocks);
        entry.size = 9;
        reinterpret_cast<void*(__thiscall*)(void*, const void*)>(addr::HTMLCacheEntry_Set)(copy, &entry);
        HashCacheEntry(f, copy, blocks);
        DestroyText(reinterpret_cast<GameText*>(copy));
        reinterpret_cast<void(__thiscall*)(void*)>(addr::SC4StringPair_Destruct)(pair);
        DestroyText(&entry.name);
        DestroyText(&key);
    }
    HashCacheMap(f, cache, blocks);
    DestroyCache(f, cache, blocks);
    return f.h;
}

bool g_nodesOn = false;
uint32_t g_nodesHash = 2166136261u;

void DriveNodesDay(const Day& day)
{
    if (!OnRunDay(day, "OPENSC4_TEST_HTML_NODES") || !game::IsBuild641())
        return;
    SetUpService();
    SetUpNodeStands();
    SetUpImageStands();
    SetUpWindowStands();
    SetUpInputStands();
    void* const face = MakeWindow("html nodes");
    if (!face)
        return;
    char* const window = static_cast<char*>(face) - 0xD8;
    const bool compare = AnyOurs();
    Fnv total;
    int items = 0;
    int differ = 0;
    uint32_t sectionHash[13] = {};
    const char* const sections[] = {"constructors", "destructors", "colours", "characters", "pages", "children",
                                    "document", "slots", "images", "window", "input", "directors", "cache"};
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
    for (int i = 0; i < ChildrenItems(); ++i)
        item(5, i, [&] { return ChildrenItem(i); });
    // h2a's: constructors, destructors, attributes, the tokenizer's helpers,
    // the handlers' pages.
    {
        int at = 0;
        for (int i = 0; i < kDocConstructorItems; ++i)
            item(6, at++, [&] { return DocConstructorItem(i); });
        for (int i = 0; i < kDocDestructorItems; ++i)
            item(6, at++, [&] { return DocDestructorItem(i); });
        for (int i = 0; i < kDocAttributeItems; ++i)
            item(6, at++, [&] { return DocAttributeItem(i); });
        for (int i = 0; i < kDocTokenItems; ++i)
            item(6, at++, [&] { return DocTokenItem(i); });
        for (int i = 0; i < kDocPageItems; ++i)
            item(6, at++, [&] { return DocPageItem(window, i); });
    }
    // h2b1's: the document's slots, its life, the parser's glue, the font
    // stack, DOCTYPE pages.
    {
        int at = 0;
        for (int i = 0; i < kSlotItems; ++i)
            item(7, at++, [&] { return DocSlotItem(window, i); });
        for (int i = 0; i < kLifeItems; ++i)
            item(7, at++, [&] { return DocLifeItem(window, i); });
        for (int i = 0; i < kGlueItems; ++i)
            item(7, at++, [&] { return DocGlueItem(window, i); });
        for (int i = 0; i < kFontItems; ++i)
            item(7, at++, [&] { return DocFontItem(window, i); });
        for (int i = 0; i < kDoctypeItems; ++i)
            item(7, at++, [&] { return DocDoctypeItem(window, i); });
    }
    // h2b2's: image entries, the image map, the release, the picture reader,
    // images loaded, the service's answers, bullets, slots 5 and 20.
    {
        int at = 0;
        for (int i = 0; i < kEntryItems; ++i)
            item(8, at++, [&] { return ImageEntryItem(i); });
        for (int i = 0; i < kMapItems; ++i)
            item(8, at++, [&] { return ImageMapItem(i); });
        for (int i = 0; i < kReleaseItems; ++i)
            item(8, at++, [&] { return ReleaseItem(window, i); });
        for (int i = 0; i < kReadItems; ++i)
            item(8, at++, [&] { return ReadItem(window, i); });
        for (int i = 0; i < kLoadItems; ++i)
            item(8, at++, [&] { return LoadItem(window, i); });
        for (int i = 0; i < kMessageItems; ++i)
            item(8, at++, [&] { return MessageItem(window, i); });
        for (int i = 0; i < kBulletItems; ++i)
            item(8, at++, [&] { return BulletItem(window, i); });
        for (int i = 0; i < kVtableSlotItems; ++i)
            item(8, at++, [&] { return VtableSlotItem(window, i); });
    }
    // h1a's: the window's life, pages, COM slots and settings.
    for (int i = 0; i < kWindowItems; ++i)
        item(9, i, [&] { return WindowItem(i); });
    // h1b's: the window's input, view and scroll bars, paint and addresses.
    for (int i = 0; i < kInputItems; ++i)
        item(10, i, [&] { return InputItem(i); });
    // h1c's: the directors and the shared block's release.
    for (int i = 0; i < kDirectorItems; ++i)
        item(11, i, [&] { return DirectorItem(i); });
    // h7a's: the file cache's entries. The 250 entries' item run once
    // first, its answer dropped: the first run of it ever grows the pools,
    // and a block freed then may not come back as freed.
    CacheItem(3);
    for (int i = 0; i < kCacheItems; ++i)
        item(12, i, [&] { return CacheItem(i); });
    Virtual<uint32_t>(face, 0x8);

    g_nodesHash = total.h;
    g_nodesOn = true;
    log::Write("autotest: html nodes: %d items (constructors %08X, destructors %08X, colours %08X, characters %08X, "
               "pages %08X, children %08X, document %08X, slots %08X, images %08X, window %08X, input %08X, directors "
               "%08X, cache %08X), %u stand-in "
               "calls, %u of %u "
               "blocks "
               "freed; %s; "
               "hash %08X",
               items, sectionHash[0], sectionHash[1], sectionHash[2], sectionHash[3], sectionHash[4], sectionHash[5],
               sectionHash[6], sectionHash[7], sectionHash[8], sectionHash[9], sectionHash[10], sectionHash[11], sectionHash[12], g_standCalls,
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
