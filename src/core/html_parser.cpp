// Our HTML parser of the game's HTML viewer (docs/html.md, piece h3 with the
// tokenizer of h2): text in, a tree of nodes out. The document (0x1C4
// bytes, vtable 0x00ACD938) keeps the page and the tokenizer's state; the
// parser reads it a tag at a time. Each function is a detour on the game's,
// in groups, off by default:
//
//   ui.html.parser.tokens   the tokenizer: the text up to the next tag
//                           (0x008FFAC2), the next tag and its ID
//                           (0x00905CF0), the tag's attributes split into
//                           the document's map (0x009056DA, 0x00901149)
//   ui.html.parser.names    the tag table (0x00902D4F, with its inserts
//                           0x0090116D and 0x00902D3B) and the named
//                           entities (0x00901222, with 0x009010FD)
//   ui.html.parser.document the document's constructor, Parse and the page,
//                           head, title and body, plain text, images asked
//                           for, the font stack
//   ui.html.parser.blocks   P, CENTER, DIV, BLOCKQUOTE, FORM, headings, PRE,
//                           DL, lists and their items, the skips
//   ui.html.parser.table    TABLE, its rows, cells and parts
//   ui.html.parser.inline   text, font and phrase tags, A, SPAN, BLINK, MAP,
//                           the form tags
//
// The first two groups are here; the handlers of the other four are in
// html_parser_tree.h.
//
// The game's strings and containers are STLport's, through the game's own
// helpers: the string bodies' erase, find, assign and append, the hash maps'
// resize and insert, the trees' insert and find. Ours call the same helpers
// with the same arguments in the same order, so the memory they leave is
// the same. Calls between the parser's functions go through the game's
// addresses (and so through the detours), so any mix of groups works.
//
// g_useGame (set by the parser test, html_parser_autotest.cpp) sends every
// detour to the game's function, so one run can parse the same page through
// the game's code and through ours and compare the trees.
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

// True while the parser test runs the game's functions (see above).
bool g_useGame = false;

namespace {

void FirstCall(const char* name, bool& logged)
{
    if (logged)
        return;
    logged = true;
    log::Write("html parser: first %s", name);
}

#define OPENSC4_FIRST(name)                                                                                    \
    do {                                                                                                       \
        static bool logged_ = false;                                                                           \
        if (!logged_)                                                                                          \
            FirstCall(name, logged_);                                                                          \
    } while (0)

// ====================================================================================
// The game's strings

// An STLport string's body: begin, end (at a terminating zero) and the end
// of its storage.
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

inline uint32_t Length(const Body& body) { return static_cast<uint32_t>(body.end - body.begin); }

constexpr uint32_t kAll = 0xFFFFFFFFu;

template <typename Fn>
Fn GameFn(std::uintptr_t address)
{
    return reinterpret_cast<Fn>(address);
}

// The string helpers, all the game's.
void Erase(Body* body, uint32_t position, uint32_t count)
{
    GameFn<Body*(__thiscall*)(Body*, uint32_t, uint32_t)>(addr::SC4String_EraseBody)(body, position, count);
}

uint32_t FindChar(Body* body, char c, uint32_t from)
{
    return GameFn<uint32_t(__thiscall*)(Body*, char, uint32_t)>(addr::SC4String_FindCharBody)(body, c, from);
}

uint32_t Find(Body* body, const char* text, uint32_t from)
{
    return GameFn<uint32_t(__thiscall*)(Body*, const char*, uint32_t)>(addr::SC4StringBody_Find)(body, text, from);
}

void Assign(Body* body, const Body* source, uint32_t position, uint32_t count)
{
    GameFn<Body*(__thiscall*)(Body*, const Body*, uint32_t, uint32_t)>(addr::SC4StringBody_AssignBody)(
        body, source, position, count);
}

void Append(Body* body, const Body* source, uint32_t position, uint32_t count)
{
    GameFn<Body*(__thiscall*)(Body*, const Body*, uint32_t, uint32_t)>(addr::SC4StringBody_AppendPart)(
        body, source, position, count);
}

void MakeEmpty(Text* text) { GameFn<Text*(__thiscall*)(Text*)>(addr::SC4String_SC4String)(text); }

void FromCString(Text* text, const char* from)
{
    GameFn<Text*(__thiscall*)(Text*, const char*)>(addr::SC4String_FromText)(text, from);
}

void CopyPart(Text* text, const Text* source, uint32_t position, uint32_t count)
{
    GameFn<Text*(__thiscall*)(Text*, const Text*, uint32_t, uint32_t)>(addr::SC4String_Substring)(text, source,
                                                                                                 position, count);
}

void AssignText(Text* text, const Text* source)
{
    GameFn<Text*(__thiscall*)(Text*, const Text*)>(addr::cRZBaseString_Assign)(text, source);
}

void Repeated(Text* text, char c, uint32_t count)
{
    GameFn<Text*(__thiscall*)(Text*, char, uint32_t)>(addr::SC4String_ConstructRepeated)(text, c, count);
}

void MakeUpper(Text* text) { GameFn<void(__thiscall*)(Text*)>(addr::cRZBaseString_MakeUpper)(text); }

void Destroy(Text* text) { GameFn<void(__thiscall*)(Text*)>(addr::SC4String_Destroy)(text); }

void Free(void* p) { GameFn<void(__cdecl*)(void*)>(addr::SC4_Free)(p); }

// What a string's destructor does, written out where the game inlines it.
void DestroyInline(Text* text)
{
    text->vtable = static_cast<uint32_t>(addr::SC4String_Vtable);
    if (text->body.begin)
        Free(text->body.begin);
}

// The viewer's character helpers (h5): the next character's position and
// whether the character at a position is a space, both multibyte-aware.
uint32_t NextChar(const Text* text, uint32_t position)
{
    return GameFn<uint32_t(__cdecl*)(const Text*, uint32_t)>(addr::HTMLText_NextChar)(text, position);
}

bool IsSpaceAt(const Text* text, uint32_t position)
{
    return GameFn<uint8_t(__cdecl*)(const Text*, uint32_t)>(addr::HTMLText_IsSpaceAt)(text, position) != 0;
}

bool IsLetter(char c) { return GameFn<uint8_t(__cdecl*)(char)>(addr::HTMLText_IsLetter)(c) != 0; }

// ====================================================================================
// The document's fields

constexpr std::size_t kPushedBack = 0x28; // byte: NextTag gives the current tag again
constexpr std::size_t kTagText = 0xD4;    // the tag's text, then its name
constexpr std::size_t kTagId = 0xE8;      // 0 none, 1 end of the page, 3 text, else the tag table's
constexpr std::size_t kTagRest = 0xEC;    // the tag's text after its name
constexpr std::size_t kRunText = 0x100;   // the text before the tag
constexpr std::size_t kAttributes = 0x114;
constexpr std::size_t kPage = 0x180;
constexpr std::size_t kPosition = 0x194; // in the page

constexpr uint32_t kTextId = 3;
constexpr uint32_t kEndId = 1;
constexpr uint32_t kDoctypeId = 2;

Text* TextAt(char* doc, std::size_t offset) { return reinterpret_cast<Text*>(doc + offset); }

// ====================================================================================
// The tokenizer (ui.html.parser.tokens)

using ReadTextFn = uint32_t(__thiscall*)(char*);
using NextTagFn = void(__thiscall*)(char*, uint8_t);
using ParseAttributesFn = void(__thiscall*)(char*);
using InsertFn = void*(__thiscall*)(void*, void*, const void*);

ReadTextFn g_readText = nullptr;
NextTagFn g_nextTag = nullptr;
ParseAttributesFn g_parseAttributes = nullptr;
InsertFn g_insertAttribute = nullptr;

bool StartsComment(const Body& page, uint32_t at)
{
    return Length(page) - at >= 4 && page.begin[at + 1] == '!' && page.begin[at + 2] == '-' &&
           page.begin[at + 3] == '-';
}

// The game's 0x00905CF0 and 0x008FFAC2 skip a comment the same way: past
// `<!--`, then past the next `-->` (or to the end).
void SkipComment(char* doc)
{
    Body& page = TextAt(doc, kPage)->body;
    uint32_t& position = At<uint32_t>(doc, kPosition);
    position += 4;
    const uint32_t close = Find(&page, "-->", position);
    position = close < Length(page) ? close + 3 : Length(page);
}

// The game's 0x008FFAC2: the run text (+0x100) emptied, then the page from
// the position up to the next `<` added to it; comments are skipped and
// the run carries on after them; any other tag ends it. True when the run
// is not empty.
bool ReadText(char* doc)
{
    Body& run = TextAt(doc, kRunText)->body;
    Body& page = TextAt(doc, kPage)->body;
    uint32_t& position = At<uint32_t>(doc, kPosition);
    Erase(&run, 0, kAll);
    if (position < Length(page)) {
        for (;;) {
            const uint32_t open = FindChar(&page, '<', position);
            if (open >= Length(page)) {
                Append(&run, &page, position, Length(page) - position);
                position = Length(page);
                break;
            }
            if (open == position) {
                if (!StartsComment(page, open))
                    break;
                // As SkipComment, with a check that something follows `<!--`.
                position += 4;
                if (position < Length(page)) {
                    const uint32_t close = Find(&page, "-->", position);
                    position = close < Length(page) ? close + 3 : Length(page);
                }
            } else if (open > position) {
                Append(&run, &page, position, open - position);
                position = open;
            }
            if (position >= Length(page))
                break;
        }
    }
    return Length(run) != 0;
}

uint32_t __fastcall DReadText(char* doc, void*)
{
    if (g_useGame)
        return g_readText(doc);
    OPENSC4_FIRST("ReadText 0x008FFAC2");
    return ReadText(doc) ? 1 : 0;
}

uint32_t CallReadText(char* doc) { return GameFn<ReadTextFn>(addr::HTMLDocument_ReadText)(doc) & 0xFF; }

// The game's 0x00905CF0. A tag given back (+0x28) is the current one again,
// unless the caller wants text and the current ID is text: then the text is
// read again. Otherwise: the run text (0x008FFAC2); ID 3 when there was some
// and the caller stops at text; ID 1 at the end of the page. Comments are
// skipped; text ending at anything but `<` (only the end of the page) reads
// on. Then the tag's text up to `>` (none: ID 0, the position after the
// `<`), its name split off (0x00900138) and looked up in the tag table
// (unknown names keep ID 0) and its attributes split (0x009056DA).
void NextTag(char* doc, bool stopAtText)
{
    if (At<uint8_t>(doc, kPushedBack)) {
        At<uint8_t>(doc, kPushedBack) = 0;
        if (!stopAtText || At<uint32_t>(doc, kTagId) != kTextId)
            return;
    }
    Body& page = TextAt(doc, kPage)->body;
    uint32_t& position = At<uint32_t>(doc, kPosition);
    uint32_t& id = At<uint32_t>(doc, kTagId);
    id = 0;
    for (;;) {
        if (CallReadText(doc)) {
            if (!stopAtText) {
                id = kTextId;
                return;
            }
        } else if (position >= Length(page)) {
            id = kEndId;
            return;
        }
        if (StartsComment(page, position)) {
            SkipComment(doc);
            continue;
        }
        if (page.begin[position] == '<')
            break;
    }
    ++position;
    const uint32_t length = Length(page);
    uint32_t at = position;
    if (at >= length)
        return;
    while (page.begin[at] != '>') {
        at = NextChar(TextAt(doc, kPage), at);
        if (at >= length)
            break;
    }
    if (at >= length)
        return;
    Assign(&TextAt(doc, kTagText)->body, &page, position, at - position);
    position = at + 1;
    GameFn<void(__cdecl*)(Text*, Text*)>(addr::HTMLDocument_SplitTagName)(TextAt(doc, kTagText),
                                                                         TextAt(doc, kTagRest));
    Text name;
    FromCString(&name, TextAt(doc, kTagText)->body.begin);
    char* const entry = GameFn<char*(__thiscall*)(void*, const Text*)>(addr::HTMLTagTable_Find)(
        reinterpret_cast<void*>(addr::HTMLTags_Table), &name);
    if (name.body.begin)
        Free(name.body.begin);
    if (entry)
        id = At<uint32_t>(entry, 0x18);
    GameFn<ParseAttributesFn>(addr::HTMLDocument_ParseAttributes)(doc);
}

void __fastcall DNextTag(char* doc, void*, uint8_t stopAtText)
{
    if (g_useGame)
        return g_nextTag(doc, stopAtText);
    OPENSC4_FIRST("NextTag 0x00905CF0");
    NextTag(doc, stopAtText != 0);
}

// The pair the attribute map holds: name and value.
struct TextPair {
    Text name;
    Text value;
};

// The game's 0x009056DA: the attribute map (+0x114) emptied and filled from
// the tag's text after its name (+0xEC, trimmed first, emptied after), except
// for `<!DOCTYPE` (ID 2). Each attribute: a name from the first character
// that is not a space on, as long as letters, `-` and `/` follow; then, past
// spaces, `=` and a value: up to the matching `"` or `'` (the character
// after the opening quote is always taken, so `""` takes the closing quote
// and reads on to the next one), or up to the next space. Names in quotes
// lose them; names are upper-cased; empty names are not kept.
void ParseAttributes(char* doc)
{
    Text name;
    Text value;
    MakeEmpty(&name);
    MakeEmpty(&value);
    if (At<uint32_t>(doc, kTagId) != kDoctypeId) {
        void* const attributes = doc + kAttributes;
        GameFn<void(__thiscall*)(void*)>(addr::HTMLAttributes_Clear)(attributes);
        Text* const tag = TextAt(doc, kTagRest);
        GameFn<void(__cdecl*)(Text*)>(addr::HTMLText_Trim)(tag);
        Body& text = tag->body;
        if (Length(text) != 0) {
            uint32_t at = 0;
            do {
                if (IsSpaceAt(tag, at)) {
                    at = NextChar(tag, at);
                    continue;
                }
                const uint32_t nameFirst = at;
                at = NextChar(tag, at);
                for (;;) {
                    const char c = text.begin[at];
                    if (!IsLetter(c) && c != '-' && c != '/')
                        break;
                    at = NextChar(tag, at);
                    if (at >= Length(text))
                        break;
                }
                const uint32_t nameLast = at - 1;
                while (at < Length(text) && IsSpaceAt(tag, at))
                    at = NextChar(tag, at);
                if (at < Length(text) && text.begin[at] == '=') {
                    ++at;
                    const char quote = text.begin[at];
                    uint32_t valueFirst;
                    uint32_t valueLast;
                    if (quote == '"' || quote == '\'') {
                        ++at;
                        valueFirst = at;
                        do {
                            valueLast = at;
                            at = NextChar(tag, at);
                        } while (at < Length(text) && text.begin[at] != quote);
                        ++at;
                    } else {
                        valueFirst = at;
                        do {
                            valueLast = at;
                            at = NextChar(tag, at);
                        } while (at < Length(text) && !IsSpaceAt(tag, at));
                    }
                    Assign(&name.body, &text, nameFirst, nameLast - nameFirst + 1);
                    Assign(&value.body, &text, valueFirst, valueLast - valueFirst + 1);
                } else {
                    Assign(&name.body, &text, nameFirst, nameLast - nameFirst + 1);
                    Erase(&value.body, 0, kAll);
                }
                if (name.body.begin[0] == '"' && name.body.end[-1] == '"') {
                    Erase(&name.body, 1, 1);
                    Erase(&name.body, Length(name.body) - 1, 1);
                }
                MakeUpper(&name);
                if (Length(name.body) != 0) {
                    TextPair pair;
                    GameFn<TextPair*(__thiscall*)(TextPair*, const Text*, const Text*)>(addr::SC4StringPair_Construct)(
                        &pair, &name, &value);
                    char result[16];
                    GameFn<InsertFn>(addr::HTMLAttributes_Insert)(attributes, result, &pair);
                    Destroy(&pair.value);
                    DestroyInline(&pair.name);
                }
            } while (at < Length(text));
        }
        Erase(&text, 0, kAll);
    }
    DestroyInline(&value);
    DestroyInline(&name);
}

void __fastcall DParseAttributes(char* doc, void*)
{
    if (g_useGame)
        return g_parseAttributes(doc);
    OPENSC4_FIRST("ParseAttributes 0x009056DA");
    ParseAttributes(doc);
}

// The game's 0x00901149 (and 0x0090116D for the tag table): the hash map
// grown for one more entry (0x008FF066), then the pair inserted without
// growing it: the attribute map keeps equal names (0x00900F4C), the tag
// table does not (0x00900FCC). Returns `result`.
void* InsertGrowing(void* map, void* result, const void* pair, std::uintptr_t insert)
{
    GameFn<void(__thiscall*)(void*, uint32_t)>(addr::HTMLStringHash_Resize)(map, At<uint32_t>(map, 0x10) + 1);
    GameFn<InsertFn>(insert)(map, result, pair);
    return result;
}

void* __fastcall DInsertAttribute(void* map, void*, void* result, const void* pair)
{
    if (g_useGame)
        return g_insertAttribute(map, result, pair);
    OPENSC4_FIRST("InsertAttribute 0x00901149");
    return InsertGrowing(map, result, pair, addr::HTMLAttributes_InsertNoResize);
}

// ====================================================================================
// The tag table and the entities (ui.html.parser.names)

using TagsInitFn = void(__cdecl*)();
using EntityFn = Text*(__thiscall*)(char*, Text*, const Text*);

TagsInitFn g_tagsInit = nullptr;
InsertFn g_insertTag = nullptr;
InsertFn g_insertTagCall = nullptr;
EntityFn g_entity = nullptr;
InsertFn g_insertEntityCall = nullptr;

struct Named {
    const char* name;
    uint32_t value;
};

// The tag table, in the game's order of insertion (0x00902D4F). Every start
// and end tag has its own ID; COL, INPUT, IMG, BR, HR, AREA and FRAME have
// no end tag. DT and DD are numbered against their order here.
constexpr Named kTags[] = {
    {"!DOCTYPE", 0x02}, {"HTML", 0x04}, {"/HTML", 0x05}, {"HEAD", 0x06}, {"/HEAD", 0x07}, {"BODY", 0x08},
    {"/BODY", 0x09}, {"TITLE", 0x0A}, {"/TITLE", 0x0B}, {"BASE", 0x0C}, {"STYLE", 0x0D}, {"/STYLE", 0x0E},
    {"META", 0x0F}, {"LINK", 0x10}, {"ISINDEX", 0x11}, {"TT", 0x12}, {"/TT", 0x13}, {"I", 0x14},
    {"/I", 0x15}, {"B", 0x16}, {"/B", 0x17}, {"STRIKE", 0x18}, {"/STRIKE", 0x19}, {"BIG", 0x1A},
    {"/BIG", 0x1B}, {"SMALL", 0x1C}, {"/SMALL", 0x1D}, {"U", 0x1E}, {"/U", 0x1F}, {"FONT", 0x20},
    {"/FONT", 0x21}, {"EM", 0x22}, {"/EM", 0x23}, {"STRONG", 0x24}, {"/STRONG", 0x25}, {"DFN", 0x26},
    {"/DFN", 0x27}, {"CODE", 0x28}, {"/CODE", 0x29}, {"SAMP", 0x2A}, {"/SAMP", 0x2B}, {"KBD", 0x2C},
    {"/KBD", 0x2D}, {"VAR", 0x2E}, {"/VAR", 0x2F}, {"CITE", 0x30}, {"/CITE", 0x31}, {"ABBR", 0x32},
    {"/ABBR", 0x33}, {"ACRONYM", 0x34}, {"/ACRONYM", 0x35}, {"A", 0x36}, {"/A", 0x37}, {"IMG", 0x38},
    {"OBJECT", 0x39}, {"/OBJECT", 0x3A}, {"BR", 0x3B}, {"SCRIPT", 0x3C}, {"/SCRIPT", 0x3D}, {"MAP", 0x3E},
    {"/MAP", 0x3F}, {"Q", 0x40}, {"/Q", 0x41}, {"SUB", 0x42}, {"/SUB", 0x43}, {"SUP", 0x44},
    {"/SUP", 0x45}, {"SPAN", 0x46}, {"/SPAN", 0x47}, {"BDO", 0x48}, {"/BDO", 0x49}, {"BLINK", 0x4A},
    {"/BLINK", 0x4B}, {"INPUT", 0x4C}, {"SELECT", 0x4D}, {"/SELECT", 0x4E}, {"OPTION", 0x4F}, {"/OPTION", 0x50},
    {"OPTGROUP", 0x51}, {"/OPTGROUP", 0x52}, {"TEXTAREA", 0x53}, {"/TEXTAREA", 0x54}, {"LABEL", 0x55},
    {"/LABEL", 0x56}, {"BUTTON", 0x57}, {"/BUTTON", 0x58}, {"H1", 0x59}, {"/H1", 0x5A}, {"H2", 0x5B},
    {"/H2", 0x5C}, {"H3", 0x5D}, {"/H3", 0x5E}, {"H4", 0x5F}, {"/H4", 0x60}, {"H5", 0x61},
    {"/H5", 0x62}, {"H6", 0x63}, {"/H6", 0x64}, {"P", 0x65}, {"/P", 0x66}, {"PRE", 0x67},
    {"/PRE", 0x68}, {"UL", 0x69}, {"/UL", 0x6A}, {"OL", 0x6B}, {"/OL", 0x6C}, {"DL", 0x6D},
    {"/DL", 0x6E}, {"DIV", 0x6F}, {"/DIV", 0x70}, {"NOSCRIPT", 0x71}, {"/NOSCRIPT", 0x72}, {"BLOCKQUOTE", 0x73},
    {"/BLOCKQUOTE", 0x74}, {"FORM", 0x75}, {"/FORM", 0x76}, {"HR", 0x77}, {"TABLE", 0x78}, {"/TABLE", 0x79},
    {"FIELDSET", 0x7A}, {"/FIELDSET", 0x7B}, {"ADDRESS", 0x7C}, {"/ADDRESS", 0x7D}, {"CENTER", 0x7E},
    {"/CENTER", 0x7F}, {"DIR", 0x80}, {"/DIR", 0x81}, {"MENU", 0x82}, {"/MENU", 0x83}, {"CAPTION", 0x84},
    {"/CAPTION", 0x85}, {"COL", 0x86}, {"COLGROUP", 0x87}, {"/COLGROUP", 0x88}, {"THEAD", 0x89},
    {"/THEAD", 0x8A}, {"TBODY", 0x8B}, {"/TBODY", 0x8C}, {"TFOOT", 0x8D}, {"/TFOOT", 0x8E}, {"TD", 0x8F},
    {"/TD", 0x90}, {"TH", 0x91}, {"/TH", 0x92}, {"TR", 0x93}, {"/TR", 0x94}, {"LI", 0x95},
    {"/LI", 0x96}, {"DD", 0x99}, {"/DD", 0x9A}, {"DT", 0x97}, {"/DT", 0x98}, {"AREA", 0x9B},
    {"FRAMESET", 0x9C}, {"/FRAMESET", 0x9D}, {"FRAME", 0x9E}, {"NOFRAMES", 0x9F}, {"/NOFRAMES", 0xA0},
    {"NOBR", 0xA1}, {"/NOBR", 0xA2},
};
static_assert(sizeof kTags / sizeof kTags[0] == 160);

// The game builds its first 29 entries one way and the rest another (see
// TagsInit); this is where the second way starts.
constexpr std::size_t kFirstPairedTag = 29;

// The named entities and their Latin-1 codes, in the game's order
// (0x00901222). The first 33 are inserted directly, the rest through the
// insert's out-of-line copy 0x009010FD.
constexpr Named kEntities[] = {
    {"quot", 34},    {"amp", 38},     {"lt", 60},      {"gt", 62},      {"nbsp", 160},   {"iexcl", 161},
    {"cent", 162},   {"pound", 163},  {"curren", 164}, {"yen", 165},    {"brvbar", 166}, {"sect", 167},
    {"uml", 168},    {"copy", 169},   {"ordf", 170},   {"laquo", 171},  {"not", 172},    {"shy", 173},
    {"reg", 174},    {"macr", 175},   {"deg", 176},    {"plusmn", 177}, {"sup2", 178},   {"sup3", 179},
    {"acute", 180},  {"micro", 181},  {"para", 182},   {"middot", 183}, {"cedil", 184},  {"sup1", 185},
    {"ordm", 186},   {"raquo", 187},  {"frac14", 188}, {"frac12", 189}, {"frac34", 190}, {"iquest", 191},
    {"Agrave", 192}, {"Aacute", 193}, {"Acirc", 194},  {"Atilde", 195}, {"Auml", 196},   {"Aring", 197},
    {"AElig", 198},  {"Ccedil", 199}, {"Egrave", 200}, {"Eacute", 201}, {"Ecirc", 202},  {"Euml", 203},
    {"Igrave", 204}, {"Iacute", 205}, {"Icirc", 206},  {"Iuml", 207},   {"ETH", 208},    {"Ntilde", 209},
    {"Ograve", 210}, {"Oacute", 211}, {"Ocirc", 212},  {"Otilde", 213}, {"Ouml", 214},   {"times", 215},
    {"Oslash", 216}, {"Ugrave", 217}, {"Uacute", 218}, {"Ucirc", 219},  {"Uuml", 220},   {"Yacute", 221},
    {"THORN", 222},  {"szlig", 223},  {"agrave", 224}, {"aacute", 225}, {"acirc", 226},  {"atilde", 227},
    {"auml", 228},   {"aring", 229},  {"aelig", 230},  {"ccedil", 231}, {"egrave", 232}, {"eacute", 233},
    {"ecirc", 234},  {"euml", 235},   {"igrave", 236}, {"iacute", 237}, {"icirc", 238},  {"iuml", 239},
    {"eth", 240},    {"ntilde", 241}, {"ograve", 242}, {"oacute", 243}, {"ocirc", 244},  {"otilde", 245},
    {"ouml", 246},   {"divide", 247}, {"oslash", 248}, {"ugrave", 249}, {"uacute", 250}, {"ucirc", 251},
    {"uuml", 252},   {"yacute", 253}, {"thorn", 254},  {"yuml", 255},
};
static_assert(sizeof kEntities / sizeof kEntities[0] == 100);

constexpr std::size_t kFirstEntityByCall = 33;

// The pair the tag table and the entity map hold: a name and a number.
struct TextNumber {
    Text name;
    uint32_t value;
};

void MakeTextNumber(TextNumber* pair, const Text* name, const uint32_t* value)
{
    GameFn<TextNumber*(__thiscall*)(TextNumber*, const Text*, const uint32_t*)>(addr::SC4StringIntPair_Construct)(
        pair, name, value);
}

// The game's 0x00902D4F: every tag of kTags into the static table
// (0x00B624A4) through 0x0090116D. Each name is made a string, then a pair
// with its ID: the first 29 copy the name into the pair (0x00408BE0) and
// free the pair's text inline (and, but for the 29th, the name's too); the
// rest make the pair with 0x00662A20, insert through 0x00902D3B and destroy
// both with the string's destructor. The two ways leave the same table.
void TagsInit()
{
    void* const table = reinterpret_cast<void*>(addr::HTMLTags_Table);
    for (std::size_t i = 0; i < sizeof kTags / sizeof kTags[0]; ++i) {
        Text name;
        FromCString(&name, kTags[i].name);
        TextNumber pair;
        char result[16];
        if (i < kFirstPairedTag) {
            CopyPart(&pair.name, &name, 0, kAll);
            pair.value = kTags[i].value;
            GameFn<InsertFn>(addr::HTMLTags_Insert)(table, result, &pair);
            if (pair.name.body.begin)
                Free(pair.name.body.begin);
            if (i + 1 == kFirstPairedTag)
                Destroy(&name);
            else if (name.body.begin)
                Free(name.body.begin);
        } else {
            const uint32_t value = kTags[i].value;
            MakeTextNumber(&pair, &name, &value);
            GameFn<InsertFn>(addr::HTMLTags_InsertCall)(table, result, &pair);
            Destroy(&pair.name);
            Destroy(&name);
        }
    }
}

void __cdecl DTagsInit()
{
    if (g_useGame)
        return g_tagsInit();
    OPENSC4_FIRST("TagsInit 0x00902D4F");
    TagsInit();
}

void* __fastcall DInsertTag(void* map, void*, void* result, const void* pair)
{
    if (g_useGame)
        return g_insertTag(map, result, pair);
    OPENSC4_FIRST("InsertTag 0x0090116D");
    return InsertGrowing(map, result, pair, addr::HTMLTagTable_InsertNoResize);
}

// 0x00902D3B and 0x009010FD: out-of-line copies of a call to the tag
// table's insert (0x0090116D) and the entity map's (0x00662EF0).
void* __fastcall DInsertTagCall(void* map, void*, void* result, const void* pair)
{
    if (g_useGame)
        return g_insertTagCall(map, result, pair);
    OPENSC4_FIRST("InsertTagCall 0x00902D3B");
    GameFn<InsertFn>(addr::HTMLTags_Insert)(map, result, pair);
    return result;
}

void* __fastcall DInsertEntityCall(void* map, void*, void* result, const void* pair)
{
    if (g_useGame)
        return g_insertEntityCall(map, result, pair);
    OPENSC4_FIRST("InsertEntityCall 0x009010FD");
    GameFn<InsertFn>(addr::SC4StringMap_InsertUnique)(map, result, pair);
    return result;
}

// The game's 0x00901222: the text of the entity `name` (without & and ;)
// into `out`. The document's entity map (a tree at +8, its count at +0xC)
// is filled on first use. A known entity's code goes through 0x00911358 as
// one ISO-8859-1 character (code page 28591); an unknown one gives `?`.
Text* Entity(char* doc, Text* out, const Text* name)
{
    void* const map = doc + 0x8;
    if (At<uint32_t>(doc, 0xC) == 0) {
        for (std::size_t i = 0; i < sizeof kEntities / sizeof kEntities[0]; ++i) {
            const uint32_t value = kEntities[i].value;
            Text text;
            FromCString(&text, kEntities[i].name);
            TextNumber pair;
            MakeTextNumber(&pair, &text, &value);
            char result[16];
            if (i < kFirstEntityByCall) {
                GameFn<InsertFn>(addr::SC4StringMap_InsertUnique)(map, result, &pair);
                DestroyInline(&pair.name);
                DestroyInline(&text);
            } else {
                GameFn<InsertFn>(addr::HTMLEntities_InsertCall)(map, result, &pair);
                Destroy(&pair.name);
                Destroy(&text);
            }
        }
    }
    char* found = nullptr;
    GameFn<void*(__thiscall*)(void*, char**, const Text*)>(addr::SC4StringMap_Find)(map, &found, name);
    Text decoded;
    MakeEmpty(&decoded);
    if (found != At<char*>(doc, 0x8)) {
        char code = At<char>(found, 0x24);
        GameFn<void(__cdecl*)(const char*, uint32_t, uint32_t, Text*)>(addr::SC4String_FromWide)(&code, 1, 28591,
                                                                                                &decoded);
    } else {
        Text unknown;
        Repeated(&unknown, '?', 1);
        AssignText(&decoded, &unknown);
        Destroy(&unknown);
    }
    CopyPart(out, &decoded, 0, kAll);
    Destroy(&decoded);
    return out;
}

Text* __fastcall DEntity(char* doc, void*, Text* out, const Text* name)
{
    if (g_useGame)
        return g_entity(doc, out, name);
    OPENSC4_FIRST("Entity 0x00901222");
    return Entity(doc, out, name);
}

#include "core/html_parser_tree.h"

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
    log::Write("html parser: %s: %zu functions ours", name, N);
    return true;
}

bool InstallTokens(registry::Mode)
{
    const Detour detours[] = {
        {addr::HTMLDocument_ReadText, reinterpret_cast<void*>(&DReadText), Slot(&g_readText)},
        {addr::HTMLDocument_NextTag, reinterpret_cast<void*>(&DNextTag), Slot(&g_nextTag)},
        {addr::HTMLDocument_ParseAttributes, reinterpret_cast<void*>(&DParseAttributes), Slot(&g_parseAttributes)},
        {addr::HTMLAttributes_Insert, reinterpret_cast<void*>(&DInsertAttribute), Slot(&g_insertAttribute)},
    };
    return InstallAll("tokens", detours);
}

bool InstallNames(registry::Mode)
{
    const Detour detours[] = {
        {addr::HTMLTags_Init, reinterpret_cast<void*>(&DTagsInit), Slot(&g_tagsInit)},
        {addr::HTMLTags_Insert, reinterpret_cast<void*>(&DInsertTag), Slot(&g_insertTag)},
        {addr::HTMLTags_InsertCall, reinterpret_cast<void*>(&DInsertTagCall), Slot(&g_insertTagCall)},
        {addr::HTMLEntities_Lookup, reinterpret_cast<void*>(&DEntity), Slot(&g_entity)},
        {addr::HTMLEntities_InsertCall, reinterpret_cast<void*>(&DInsertEntityCall), Slot(&g_insertEntityCall)},
    };
    return InstallAll("names", detours);
}

bool InstallDocument(registry::Mode)
{
    const Detour detours[] = {
        {addr::HTMLDocument_Parse, reinterpret_cast<void*>(&DParse), Slot(&g_Parse)},
        {addr::HTMLDocument_ParseFromHTML, reinterpret_cast<void*>(&DParseFromHTML), Slot(&g_ParseFromHTML)},
        {addr::HTMLDocument_ParseHTML, reinterpret_cast<void*>(&DParseHTML), Slot(&g_ParseHTML)},
        {addr::HTMLDocument_ParseHead, reinterpret_cast<void*>(&DParseHead), Slot(&g_ParseHead)},
        {addr::HTMLDocument_ParseTitle, reinterpret_cast<void*>(&DParseTitle), Slot(&g_ParseTitle)},
        {addr::HTMLDocument_ParseBody, reinterpret_cast<void*>(&DParseBody), Slot(&g_ParseBody)},
        {addr::HTMLDocument_ParsePlain, reinterpret_cast<void*>(&DParsePlain), Slot(&g_ParsePlain)},
        {addr::HTMLDocument_AddImage, reinterpret_cast<void*>(&DAddImage), Slot(&g_AddImage)},
        {addr::HTMLImageMap_InsertUnique, reinterpret_cast<void*>(&DInsertImage), Slot(&g_InsertImage)},
        {addr::HTMLDocument_Construct, reinterpret_cast<void*>(&DConstruct), Slot(&g_Construct)},
        {addr::HTMLDocument_PushFont, reinterpret_cast<void*>(&DPushFontRecord), Slot(&g_PushFontRecord)},
        {addr::HTMLFontStack_Push, reinterpret_cast<void*>(&DPushFontCopy), Slot(&g_PushFontCopy)},
    };
    return InstallAll("document", detours);
}

bool InstallBlocks(registry::Mode)
{
    const Detour detours[] = {
        {addr::HTMLDocument_ParseBlock, reinterpret_cast<void*>(&DParseBlock), Slot(&g_ParseBlock)},
        {addr::HTMLDocument_SkipTo, reinterpret_cast<void*>(&DSkipToTag), Slot(&g_SkipToTag)},
        {addr::HTMLDocument_SkipScript, reinterpret_cast<void*>(&DSkipScriptTag), Slot(&g_SkipScriptTag)},
        {addr::HTMLDocument_ParseParagraph, reinterpret_cast<void*>(&DParseParagraph), Slot(&g_ParseParagraph)},
        {addr::HTMLDocument_ParseAligned, reinterpret_cast<void*>(&DParseAligned), Slot(&g_ParseAligned)},
        {addr::HTMLDocument_ParseCenter, reinterpret_cast<void*>(&DParseCenter), Slot(&g_ParseCenter)},
        {addr::HTMLDocument_ParseDiv, reinterpret_cast<void*>(&DParseDiv), Slot(&g_ParseDiv)},
        {addr::HTMLDocument_ParseQuote, reinterpret_cast<void*>(&DParseQuote), Slot(&g_ParseQuote)},
        {addr::HTMLDocument_ParseForm, reinterpret_cast<void*>(&DParseForm), Slot(&g_ParseForm)},
        {addr::HTMLDocument_ParseHeading, reinterpret_cast<void*>(&DParseHeading), Slot(&g_ParseHeading)},
        {addr::HTMLDocument_ParsePre, reinterpret_cast<void*>(&DParsePre), Slot(&g_ParsePre)},
        {addr::HTMLDocument_ParseDefinitions, reinterpret_cast<void*>(&DParseDefinitions), Slot(&g_ParseDefinitions)},
        {addr::HTMLDocument_ParseDD, reinterpret_cast<void*>(&DParseDD), Slot(&g_ParseDD)},
        {addr::HTMLDocument_ParseDT, reinterpret_cast<void*>(&DParseDT), Slot(&g_ParseDT)},
        {addr::HTMLDocument_ParseList, reinterpret_cast<void*>(&DParseList), Slot(&g_ParseList)},
        {addr::HTMLDocument_ParseBullets, reinterpret_cast<void*>(&DParseBullets), Slot(&g_ParseBullets)},
        {addr::HTMLDocument_ParseNumbers, reinterpret_cast<void*>(&DParseNumbers), Slot(&g_ParseNumbers)},
        {addr::HTMLDocument_ParseItem, reinterpret_cast<void*>(&DParseItem), Slot(&g_ParseItem)},
    };
    return InstallAll("blocks", detours);
}

bool InstallTable(registry::Mode)
{
    const Detour detours[] = {
        {addr::HTMLDocument_ParseTable, reinterpret_cast<void*>(&DParseTable), Slot(&g_ParseTable)},
        {addr::HTMLDocument_ParseRow, reinterpret_cast<void*>(&DParseRow), Slot(&g_ParseRow)},
        {addr::HTMLDocument_ParseCell, reinterpret_cast<void*>(&DParseCell), Slot(&g_ParseCell)},
        {addr::HTMLDocument_ParseHeaderCell, reinterpret_cast<void*>(&DParseHeaderCell), Slot(&g_ParseHeaderCell)},
        {addr::HTMLDocument_ParseTableBody, reinterpret_cast<void*>(&DParseTableBody), Slot(&g_ParseTableBody)},
        {addr::HTMLDocument_ParseTableFoot, reinterpret_cast<void*>(&DParseTableFoot), Slot(&g_ParseTableFoot)},
        {addr::HTMLDocument_ParseTableHead, reinterpret_cast<void*>(&DParseTableHead), Slot(&g_ParseTableHead)},
        {addr::HTMLDocument_SkipColumns, reinterpret_cast<void*>(&DSkipColumns), Slot(&g_SkipColumns)},
    };
    return InstallAll("table", detours);
}

bool InstallInline(registry::Mode)
{
    const Detour detours[] = {
        {addr::HTMLDocument_ParseInline, reinterpret_cast<void*>(&DParseInline), Slot(&g_ParseInline)},
        {addr::HTMLDocument_ParseFontTag, reinterpret_cast<void*>(&DParseFontTag), Slot(&g_ParseFontTag)},
        {addr::HTMLDocument_ParsePhrase, reinterpret_cast<void*>(&DParsePhrase), Slot(&g_ParsePhrase)},
        {addr::HTMLDocument_ParseSpecial, reinterpret_cast<void*>(&DParseSpecial), Slot(&g_ParseSpecial)},
        {addr::HTMLDocument_ParseLink, reinterpret_cast<void*>(&DParseLink), Slot(&g_ParseLink)},
        {addr::HTMLDocument_ParseSpan, reinterpret_cast<void*>(&DParseSpan), Slot(&g_ParseSpan)},
        {addr::HTMLDocument_ParseBlink, reinterpret_cast<void*>(&DParseBlink), Slot(&g_ParseBlink)},
        {addr::HTMLDocument_ParseMap, reinterpret_cast<void*>(&DParseMap), Slot(&g_ParseMap)},
        {addr::HTMLDocument_ParseFormInline, reinterpret_cast<void*>(&DParseFormInline), Slot(&g_ParseFormInline)},
        {addr::HTMLDocument_ParseSelect, reinterpret_cast<void*>(&DParseSelect), Slot(&g_ParseSelect)},
        {addr::HTMLDocument_ParseOption, reinterpret_cast<void*>(&DParseOption), Slot(&g_ParseOption)},
    };
    return InstallAll("inline", detours);
}

const bool g_registered[] = {
    registry::Register({"ui.html.parser.tokens", "ui/html", registry::Kind::Detour, registry::Phase::Attach,
                        addr::HTMLDocument_NextTag,
                        "Our HTML tokenizer: the text up to a tag, the next tag and its ID, the tag's attributes "
                        "(4 detours; docs/html.md)",
                        false, InstallTokens}),
    registry::Register({"ui.html.parser.names", "ui/html", registry::Kind::Detour, registry::Phase::Attach,
                        addr::HTMLTags_Init,
                        "Our HTML tag table and named entities, as tables (5 detours; docs/html.md)", false,
                        InstallNames}),
    registry::Register({"ui.html.parser.document", "ui/html", registry::Kind::Detour, registry::Phase::Attach,
                        addr::HTMLDocument_Parse,
                        "Our HTML document: its constructor, Parse, the page, head, title and body, plain text, "
                        "images asked for, the font stack (12 detours; docs/html.md)",
                        false, InstallDocument}),
    registry::Register({"ui.html.parser.blocks", "ui/html", registry::Kind::Detour, registry::Phase::Attach,
                        addr::HTMLDocument_ParseBlock,
                        "Our HTML block tags: P, CENTER, DIV, BLOCKQUOTE, FORM, headings, PRE, DL, UL, OL, LI and "
                        "the skips (18 detours; docs/html.md)",
                        false, InstallBlocks}),
    registry::Register({"ui.html.parser.table", "ui/html", registry::Kind::Detour, registry::Phase::Attach,
                        addr::HTMLDocument_ParseTable,
                        "Our HTML tables: TABLE, TR, TD, TH, THEAD, TBODY, TFOOT, COLGROUP (8 detours; "
                        "docs/html.md)",
                        false, InstallTable}),
    registry::Register({"ui.html.parser.inline", "ui/html", registry::Kind::Detour, registry::Phase::Attach,
                        addr::HTMLDocument_ParseInline,
                        "Our HTML inline tags: text, font and phrase tags, A, SPAN, BLINK, MAP, the form tags "
                        "(11 detours; docs/html.md)",
                        false, InstallInline}),
};

} // namespace
} // namespace opensc4::html_parser
