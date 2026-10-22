// The GZ framework's UI file parser and its director (docs/gzui.md, part u4),
// on the game's own objects. Replacements, each off by default:
//
//   framework.xmlparse.parser   the parser 0xBA2E7953 (vtable 0x00AD4718):
//                               its factory, constructor, destructors,
//                               QueryInterface, SetListener, SetContext and
//                               the three ways in (a string, a file, a
//                               resource)
//   framework.xmlparse.tokens   the parse itself: the state machine, the
//                               tokenizer and the steps it takes (the start
//                               and end of the document, text, a tag, an
//                               attribute's name and value, the attribute
//                               list), and a name resolved to a number
//   framework.xmlparse.stack    the parser's stack of variable tables:
//                               push, pop, a name looked up from the top,
//                               and slots 9 to 12 on the top table
//   framework.xmlparse.table    the variable table 0xBA2E7954 (vtable
//                               0x00AD4764): its factory, constructor,
//                               destructors, QueryInterface, SetNumber,
//                               Set, Remove, Get, Clear
//   framework.xmlparse.director the parser's director (its constructor and
//                               the static one's getter), and in the range
//                               the next director's deleting destructor and
//                               its five factories
//
// Still the game's: the two directors' one-instruction ID getters, and the
// STLport code of the attribute vector (a vector of name and value pairs
// with its own allocator) and of the table's map of game strings to values,
// which the classes call by address. Ours call each other directly, so a
// group that is off still runs ours where another group's function calls
// it.
//
// The game's methods answer a byte (AL) over whatever its last call left
// in EAX; ours give the same EAX where it comes from a call or a value and
// 0 above AL where the game leaves its caller's EAX.
//
// Every detour is entered through a thunk that keeps ECX and EDX. The
// groups go in at Attach: the UI script tree makes the parser while the
// game starts and reads every UI file with it.
#include "core/address_check.h"
#include "core/game.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"
#include "core/thunk.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>

namespace opensc4::gzui {
namespace {

// ====================================================================================
// Helpers

template <typename R, typename... Args>
inline R CallThis(std::uintptr_t address, void* self, Args... args)
{
    return reinterpret_cast<R(__thiscall*)(void*, Args...)>(address)(self, args...);
}

template <typename R, typename... Args>
inline R CallCdecl(std::uintptr_t address, Args... args)
{
    return reinterpret_cast<R(__cdecl*)(Args...)>(address)(args...);
}

template <typename T>
inline T& At(void* base, std::size_t offset)
{
    return *reinterpret_cast<T*>(static_cast<char*>(base) + offset);
}

template <typename R, typename... Args>
inline R Virtual(void* object, std::size_t byteOffset, Args... args)
{
    void** const vtable = *reinterpret_cast<void***>(object);
    return reinterpret_cast<R(__thiscall*)(void*, Args...)>(vtable[byteOffset / 4])(object, args...);
}

inline uint32_t Bits(const void* pointer) { return static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(pointer)); }

// A byte answer over what the game leaves in the rest of EAX.
inline uint32_t Answer(uint32_t eax, bool value) { return (eax & ~0xFFu) | (value ? 1u : 0u); }
inline bool Yes(uint32_t eax) { return (eax & 0xFF) != 0; }

inline uint32_t AddRef(void* object) { return Virtual<uint32_t>(object, 0x4); }
inline uint32_t Release(void* object) { return Virtual<uint32_t>(object, 0x8); }

void* New(uint32_t size) { return CallCdecl<void*>(addr::SC4_operator_new, size); }
void Delete(void* p) { CallCdecl<void>(addr::SC4_operator_delete, p); }
uint32_t Free(void* p) { return CallCdecl<uint32_t>(addr::SC4_Free, p); }

void FirstCall(const char* name, bool& logged)
{
    if (logged)
        return;
    logged = true;
    log::Write("gzui: xmlparse: first %s", name);
}

#define OPENSC4_FIRST(name)                                                                                    \
    do {                                                                                                       \
        static bool logged_ = false;                                                                           \
        if (!logged_)                                                                                          \
            FirstCall(name, logged_);                                                                          \
    } while (0)

// A game string on the stack (0x14 bytes, its text from +4).
struct alignas(4) TempString {
    char bytes[0x14];
};
inline void* Body(TempString& string) { return At<void*>(&string, 0x4); }

// A game string's text freed as the game's inline destructor frees it after
// these temporaries (the vtable is not stored again); EAX the free's, else
// `eax`.
uint32_t FreeBody(TempString& string, uint32_t eax) { return Body(string) ? Free(Body(string)) : eax; }

constexpr uint32_t kIID_cIGZUnknown = 1;
constexpr uint32_t kIID_Parser = 0x5A2C1570;
constexpr uint32_t kIID_Vars = 0x1A2BDEBB;
constexpr uint32_t kIID_TagEvents = 0x1A2BDEB9;
constexpr uint32_t kIID_AttributeEvents = 0x1A2BDEBA;
constexpr uint32_t kIID_Seekable = 0xC56B8F08;
constexpr uint32_t kClsid_Parser = 0xBA2E7953;
constexpr uint32_t kClsid_Vars = 0xBA2E7954;

// The parser (0x5C bytes).
constexpr std::size_t kListener = 0x8;          // the listener, slots 3 to 5
constexpr std::size_t kTagEvents = 0xC;         // its interface 0x1A2BDEB9
constexpr std::size_t kAttributeEvents = 0x10;  // its interface 0x1A2BDEBA
constexpr std::size_t kStack = 0x14;            // vector of variable tables: begin, end, capacity
constexpr std::size_t kContext = 0x20;          // SetContext's object, never read
constexpr std::size_t kState = 0x24;
constexpr std::size_t kBuffer = 0x28;
constexpr std::size_t kCursor = 0x2C;
constexpr std::size_t kInTag = 0x30;            // byte
constexpr std::size_t kTag = 0x34;
constexpr std::size_t kHaveName = 0x38;         // byte
constexpr std::size_t kName = 0x3C;
constexpr std::size_t kValue = 0x40;
constexpr std::size_t kAttributes = 0x44;       // vector of (name, value): begin, end, capacity, allocator

// The token types.
constexpr int32_t kText = 0;
constexpr int32_t kWord = 1;
constexpr int32_t kOpen = 2;
constexpr int32_t kClose = 3;
constexpr int32_t kEquals = 4;

// The states.
constexpr uint32_t kOutside = 1;
constexpr uint32_t kTagName = 2;
constexpr uint32_t kInside = 3;
constexpr uint32_t kAfterName = 4;
constexpr uint32_t kAfterEquals = 5;
constexpr uint32_t kSkipTag = 6;
constexpr uint32_t kSkipName = 7;
constexpr uint32_t kSkipValue = 8;
constexpr uint32_t kStop = 9;
constexpr uint32_t kFailed = 10;

inline void* Top(char* self) { return At<void**>(self, kStack + 4)[-1]; }
inline uint32_t Depth(char* self)
{
    return static_cast<uint32_t>(At<char*>(self, kStack + 4) - At<char*>(self, kStack)) >> 2;
}
inline uint32_t AttributeCount(char* self)
{
    return static_cast<uint32_t>(At<char*>(self, kAttributes + 4) - At<char*>(self, kAttributes)) >> 3;
}

// ====================================================================================
// The variable table 0xBA2E7954 (0x14 bytes: vtable, count, a map of game
// strings to counted values at +8)

constexpr std::size_t kMap = 0x8;

// A map entry as the game builds it on the stack: the key and the value.
struct alignas(4) MapPair {
    TempString key;
    void* value;
};

// The game's insert_unique's answer: the node and whether it went in.
struct alignas(4) Inserted {
    void* node;
    uint8_t in;
};

// The game's 0x009453D4.
char* __fastcall VarsConstruct(char* self, void*)
{
    OPENSC4_FIRST("VarsConstruct");
    At<uint32_t>(self, 0x4) = 0;
    At<std::uintptr_t>(self, 0) = addr::cGZUIParserVars_Vtable;
    CallThis<void*>(addr::SC4StringObjMap_Construct, self + kMap);
    return self;
}

// The game's 0x00944478.
void* __fastcall VarsNew(void*, void*)
{
    OPENSC4_FIRST("VarsNew");
    char* const memory = static_cast<char*>(New(0x14));
    return memory ? VarsConstruct(memory, nullptr) : nullptr;
}

// The game's 0x00945310: every value released, then the map emptied.
uint32_t __fastcall VarsClear(char* self, void*)
{
    OPENSC4_FIRST("VarsClear");
    char* const map = self + kMap;
    for (char* node = At<char*>(At<char*>(map, 0), 0x8); node != At<char*>(map, 0);
         node = CallCdecl<char*>(addr::cSC4Advisor_F40D4C0, node))
        Release(At<void*>(node, 0x24));
    // The map's clear leaves EAX alone when it is empty and ends with its
    // head otherwise: the head either way.
    CallThis<uint32_t>(addr::SC4StringObjMap_Clear, map);
    return Answer(Bits(At<char*>(map, 0)), true);
}

// The game's 0x009453ED.
uint32_t __fastcall VarsDestruct(char* self, void*)
{
    OPENSC4_FIRST("VarsDestruct");
    At<std::uintptr_t>(self, 0) = addr::cGZUIParserVars_Vtable;
    VarsClear(self, nullptr);
    return CallThis<uint32_t>(addr::SC4StringObjMap_Destruct, self + kMap);
}

// The game's 0x0094550B.
char* __fastcall VarsDeletingDestructor(char* self, void*, uint32_t flags)
{
    OPENSC4_FIRST("VarsDeletingDestructor");
    VarsDestruct(self, nullptr);
    if (flags & 1)
        Delete(self);
    return self;
}

// The game's 0x009444FA. A refusal leaves iid - 0x1A2BDEBB above AL.
uint32_t __fastcall VarsQueryInterface(char* self, void*, uint32_t iid, void** out)
{
    OPENSC4_FIRST("VarsQueryInterface");
    if (iid != kIID_cIGZUnknown && iid != kIID_Vars)
        return Answer(iid - kIID_Vars, false);
    *out = self;
    return Answer(AddRef(self), true);
}

// The value inserted under the name when the name is not there yet (the
// game's insert_unique on a stack copy of the pair).
Inserted Insert(char* self, const void* name, void* value, uint32_t& eax)
{
    TempString text;
    CallThis<void*>(addr::SC4String_FromIGZString, &text, name);
    MapPair pair;
    CallThis<void*>(addr::SC4String_Substring, &pair.key, &text, uint32_t{0}, uint32_t{0xFFFFFFFF});
    pair.value = value;
    Inserted inserted;
    eax = Bits(CallThis<void*>(addr::SC4StringObjMap_InsertUnique, self + kMap, &inserted, &pair));
    eax = FreeBody(pair.key, eax);
    eax = FreeBody(text, eax);
    return inserted;
}

// The game's 0x00945527: a number value (0x00915D6D) under the name; a name
// already there keeps its value and the new one is destroyed (its slot
// 0x1FC with 1).
uint32_t __fastcall VarsSetNumber(char* self, void*, const void* name, uint32_t number)
{
    OPENSC4_FIRST("VarsSetNumber");
    void* const memory = New(0x14);
    void* const value = memory ? CallThis<void*>(addr::SC4ScriptNumber_FromUint, memory, number) : nullptr;
    uint32_t eax = 0;
    const Inserted inserted = Insert(self, name, value, eax);
    if (inserted.in)
        eax = AddRef(value);
    else if (value)
        eax = Virtual<uint32_t>(value, 0x1FC, uint32_t{1});
    return Answer(eax, inserted.in != 0);
}

// The game's 0x009455C1: the value under the name, a reference taken when
// it goes in.
uint32_t __fastcall VarsSet(char* self, void*, const void* name, void* value)
{
    OPENSC4_FIRST("VarsSet");
    uint32_t eax = 0;
    const Inserted inserted = Insert(self, name, value, eax);
    if (inserted.in)
        eax = AddRef(value);
    return Answer(eax, inserted.in != 0);
}

// The node of the name, or the map's head.
char* Find(char* self, const void* name, uint32_t& eax)
{
    TempString text;
    CallThis<void*>(addr::SC4String_FromIGZString, &text, name);
    char* const node = CallThis<char*>(addr::SC4StringObjMap_Find, self + kMap, &text);
    eax = FreeBody(text, Bits(node));
    return node;
}

// The game's 0x009452A1.
uint32_t __fastcall VarsRemove(char* self, void*, const void* name)
{
    OPENSC4_FIRST("VarsRemove");
    uint32_t eax = 0;
    char* const node = Find(self, name, eax);
    if (node == At<char*>(self, kMap))
        return Answer(eax, false);
    Release(At<void*>(node, 0x24));
    eax = CallThis<uint32_t>(addr::SC4StringObjMap_Erase, self + kMap, node);
    return Answer(eax, true);
}

// The game's 0x00944BDE: the value with a reference taken.
uint32_t __fastcall VarsGet(char* self, void*, const void* name, void** out)
{
    OPENSC4_FIRST("VarsGet");
    uint32_t eax = 0;
    char* const node = Find(self, name, eax);
    if (node == At<char*>(self, kMap))
        return Answer(eax, false);
    void* const value = At<void*>(node, 0x24);
    *out = value;
    return Answer(AddRef(value), true);
}

// ====================================================================================
// The parser's stack of variable tables

// The game's 0x00944C4B (slot 13).
uint32_t __fastcall ParserPushVars(char* self, void*, void* table)
{
    OPENSC4_FIRST("ParserPushVars");
    if (!table)
        return Answer(0, false);
    AddRef(table);
    const uint32_t eax = CallThis<uint32_t>(addr::SC4PtrVector_PushBack8FF785, self + kStack, &table);
    return Answer(eax, true);
}

// The game's 0x009448E4 (slot 14): the bottom table stays.
uint32_t __fastcall ParserPopVars(char* self, void*)
{
    OPENSC4_FIRST("ParserPopVars");
    const uint32_t depth = Depth(self);
    if (depth <= 1)
        return Answer(depth, false);
    const uint32_t eax = Release(Top(self));
    At<char*>(self, kStack + 4) -= 4;
    return Answer(eax, true);
}

// The game's 0x0094490C: the name's value from the top table down.
uint32_t __fastcall ParserLookup(char* self, void*, const void* name, void** out)
{
    OPENSC4_FIRST("ParserLookup");
    uint32_t eax = 0;
    for (int32_t i = static_cast<int32_t>(Depth(self)) - 1; i >= 0; --i) {
        eax = Virtual<uint32_t>(At<void**>(self, kStack)[i], 0x18, name, out);
        if (Yes(eax))
            return Answer(eax, true);
    }
    return Answer(eax, false);
}

// The game's 0x0094489C (slot 9): a number value made for the top table's
// Set (whose EAX it answers).
uint32_t __fastcall ParserSetNumber(char* self, void*, const void* name, uint32_t number)
{
    OPENSC4_FIRST("ParserSetNumber");
    void* const memory = New(0x14);
    void* const value = memory ? CallThis<void*>(addr::SC4ScriptNumber_FromUint, memory, number) : nullptr;
    return Virtual<uint32_t>(Top(self), 0x10, name, value);
}

// The game's 0x00944891, 0x009448CE, 0x009448D9 (slots 10 to 12): the top
// table's Set, Remove and Clear.
uint32_t __fastcall ParserSetVar(char* self, void*, const void* name, void* value)
{
    OPENSC4_FIRST("ParserSetVar");
    return Virtual<uint32_t>(Top(self), 0x10, name, value);
}

uint32_t __fastcall ParserRemoveVar(char* self, void*, const void* name)
{
    OPENSC4_FIRST("ParserRemoveVar");
    return Virtual<uint32_t>(Top(self), 0x14, name);
}

uint32_t __fastcall ParserClearVars(char* self, void*)
{
    OPENSC4_FIRST("ParserClearVars");
    return Virtual<uint32_t>(Top(self), 0x1C);
}

// ====================================================================================
// The parse

// The game's 0x00944940: a name's number, from a variable of type 6 or from
// text that starts with a digit (0x0090FE93).
uint32_t __fastcall ParserResolveNumber(char* self, void*, void* name, uint32_t* out)
{
    OPENSC4_FIRST("ParserResolveNumber");
    void* value = nullptr;
    if (Yes(ParserLookup(self, nullptr, name, &value))) {
        bool number = false;
        if (Virtual<uint32_t>(value, 0xC) == 6) {
            *out = *Virtual<uint32_t*>(value, 0x124);
            number = true;
        }
        return Answer(Release(value), number);
    }
    const int32_t first = *Virtual<const signed char*>(name, 0x18);
    if (!CallCdecl<int32_t>(addr::SC4_isdigit, first))
        return 0;
    const uint32_t eax = CallCdecl<uint32_t>(addr::SC4_TextToUint32, Virtual<const char*>(name, 0x18));
    *out = eax;
    return Answer(eax, true);
}

// The game's 0x009445B2.
uint32_t __fastcall ParserStartDocument(char* self, void*)
{
    OPENSC4_FIRST("ParserStartDocument");
    uint32_t eax = Bits(self);
    At<uint8_t>(self, kInTag) = 0;
    if (void* const listener = At<void*>(self, kListener))
        eax = Virtual<uint32_t>(listener, 0xC, self);
    return Answer(eax, true);
}

// The game's 0x00944636.
uint32_t __fastcall ParserText(char* self, void*, void* text)
{
    OPENSC4_FIRST("ParserText");
    uint32_t eax = 0;
    if (void* const listener = At<void*>(self, kListener))
        eax = Virtual<uint32_t>(listener, 0x14, self, text);
    return Answer(eax, true);
}

// The game's 0x00944EEA: the attributes' values released, the list emptied,
// the pending value released.
uint32_t __fastcall ParserClearAttributes(char* self, void*)
{
    OPENSC4_FIRST("ParserClearAttributes");
    for (uint32_t i = 0; i < AttributeCount(self); ++i)
        if (void* const value = At<void**>(self, kAttributes)[2 * i + 1])
            Release(value);
    char* const begin = At<char*>(self, kAttributes);
    uint32_t eax = CallThis<uint32_t>(addr::SC4AttrVector_Erase, self + kAttributes, begin,
                                      At<char*>(self, kAttributes + 4));
    if (void* const value = At<void*>(self, kValue)) {
        eax = Release(value);
        At<void*>(self, kValue) = nullptr;
    }
    return Answer(eax, true);
}

// The game's 0x00944F7C and 0x00944F99.
uint32_t EndDocument(char* self, uint32_t done)
{
    uint32_t eax = ParserClearAttributes(self, nullptr);
    if (void* const listener = At<void*>(self, kListener))
        eax = Virtual<uint32_t>(listener, 0x10, self, done);
    return Answer(eax, true);
}

uint32_t __fastcall ParserEndFailed(char* self, void*)
{
    OPENSC4_FIRST("ParserEndFailed");
    return EndDocument(self, 0);
}

uint32_t __fastcall ParserEndDone(char* self, void*)
{
    OPENSC4_FIRST("ParserEndDone");
    return EndDocument(self, 1);
}

// The game's 0x00944F3C: a tag's name resolved to its number.
uint32_t __fastcall ParserBeginTag(char* self, void*, void* name)
{
    OPENSC4_FIRST("ParserBeginTag");
    uint32_t tag = 0;
    const uint32_t eax = ParserResolveNumber(self, nullptr, name, &tag);
    if (!Yes(eax))
        return Answer(eax, false);
    At<uint32_t>(self, kTag) = tag;
    At<uint8_t>(self, kInTag) = 1;
    if (void* const events = At<void*>(self, kTagEvents))
        Virtual<uint32_t>(events, 0xC, self, tag);
    return Answer(ParserClearAttributes(self, nullptr), true);
}

// The game's 0x009449BB: an attribute's name resolved to its number.
uint32_t __fastcall ParserSetAttributeName(char* self, void*, void* name)
{
    OPENSC4_FIRST("ParserSetAttributeName");
    const uint32_t eax = ParserResolveNumber(self, nullptr, name, &At<uint32_t>(self, kName));
    if (!Yes(eax))
        return Answer(eax, false);
    At<uint8_t>(self, kHaveName) = 1;
    return Answer(eax, true);
}

// The game's 0x009445FD: a quoted value, a script value (0x009166D4) given
// the text by its slot 0xE0.
uint32_t __fastcall ParserMakeStringValue(char* self, void*, void* text)
{
    OPENSC4_FIRST("ParserMakeStringValue");
    void* const memory = New(0x14);
    void* const value = memory ? CallThis<void*>(addr::SC4ScriptNumber_Construct, memory) : nullptr;
    At<void*>(self, kValue) = value;
    AddRef(value);
    const uint32_t eax = Virtual<uint32_t>(At<void*>(self, kValue), 0xE0, text);
    return Answer(eax, true);
}

// The game's 0x009449DD: a bare value, the variable of that name, else a
// number (a variable of type 6 or digits), else the text as a script value.
uint32_t __fastcall ParserMakeValue(char* self, void*, void* text)
{
    OPENSC4_FIRST("ParserMakeValue");
    const uint32_t found = ParserLookup(self, nullptr, text, &At<void*>(self, kValue));
    if (Yes(found))
        return Answer(found, true);
    uint32_t number = 0;
    const bool isNumber = Yes(ParserResolveNumber(self, nullptr, text, &number));
    void* const memory = New(0x14);
    void* value = nullptr;
    if (memory)
        value = isNumber ? CallThis<void*>(addr::SC4ScriptNumber_FromUint, memory, number)
                         : CallThis<void*>(addr::SC4ScriptNumber_FromString, memory, text);
    At<void*>(self, kValue) = value;
    return Answer(AddRef(value), true);
}

// The game's 0x00944AC8: a named attribute goes on the list (holding a
// reference to its value) and to the tag events' slot 5.
uint32_t __fastcall ParserCommitAttribute(char* self, void*)
{
    OPENSC4_FIRST("ParserCommitAttribute");
    if (!At<uint8_t>(self, kHaveName))
        return Answer(0, true);
    uint32_t empty[2] = {0, 0};
    CallThis<uint32_t>(addr::SC4AttrVector_PushBack, self + kAttributes, empty);
    uint32_t* const back = At<uint32_t*>(self, kAttributes + 4) - 2;
    back[0] = At<uint32_t>(self, kName);
    back[1] = At<uint32_t>(self, kValue);
    uint32_t eax = Bits(At<uint32_t*>(self, kAttributes + 4));
    if (void* const value = At<void*>(self, kValue))
        eax = AddRef(value);
    if (void* const events = At<void*>(self, kTagEvents))
        eax = Virtual<uint32_t>(events, 0x14, self, At<uint32_t>(self, kName), At<void*>(self, kValue));
    if (void* const value = At<void*>(self, kValue)) {
        eax = Release(value);
        At<void*>(self, kValue) = nullptr;
    }
    At<uint8_t>(self, kHaveName) = 0;
    return Answer(eax, true);
}

// The game's 0x009445C8: the end of a start tag, to the tag events' slot 4
// and the attribute events' slot 3 with the list.
uint32_t __fastcall ParserFinishTag(char* self, void*)
{
    OPENSC4_FIRST("ParserFinishTag");
    uint32_t eax = 0;
    if (At<uint8_t>(self, kInTag)) {
        if (void* const events = At<void*>(self, kTagEvents))
            eax = Virtual<uint32_t>(events, 0x10, self, At<uint32_t>(self, kTag));
        if (void* const events = At<void*>(self, kAttributeEvents))
            eax = Virtual<uint32_t>(events, 0xC, self, At<uint32_t>(self, kTag), self + kAttributes);
        At<uint8_t>(self, kInTag) = 0;
    }
    return Answer(eax, true);
}

inline bool Space(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

// The game's 0x00944D45: the next token as a new game string (0x00578390,
// a reference taken). Outside a tag (`textMode`) everything up to `<` is
// text; else whitespace is skipped and a token is `"..."` (text, the
// quotes dropped), `<`, `>`, `=` or a word up to one of ` \t\n\r"<>=`.
uint32_t __fastcall ParserNextToken(char* self, void*, void** out, int32_t* type, uint32_t textMode)
{
    OPENSC4_FIRST("ParserNextToken");
    if (!At<char*>(self, kCursor))
        return 0;
    void* const memory = New(0x14);
    void* const token = memory ? CallThis<void*>(addr::SC4String_SC4String, memory) : nullptr;
    *out = token;
    if (!token)
        return 0;
    AddRef(token);
    char*& cursor = At<char*>(self, kCursor);
    if ((textMode & 0xFF) && *cursor && *cursor != '<') {
        const char* const start = cursor;
        while (*cursor != '<') {
            ++cursor;
            if (!*cursor)
                break;
        }
        Virtual<uint32_t>(*out, 0xC, start, static_cast<uint32_t>(cursor - start));
        *type = kText;
        return Answer(Bits(type), true);
    }
    while (Space(*cursor))
        ++cursor;
    const char* const start = cursor;
    const char c = *start;
    if (!c) {
        const uint32_t eax = Release(*out);
        *out = nullptr;
        return Answer(eax, false);
    }
    if (c == '"') {
        ++cursor;
        const char* const body = cursor;
        if (!*body)
            return Answer(Release(*out), false);
        while (*cursor && *cursor != '"')
            ++cursor;
        const uint32_t length = static_cast<uint32_t>(cursor - body);
        if (*cursor == '"')
            ++cursor;
        Virtual<uint32_t>(*out, 0xC, body, length);
        *type = kText;
        return Answer(Bits(type), true);
    }
    if (c == '<' || c == '>' || c == '=') {
        const std::uintptr_t text = c == '<' ? addr::cGZUIParser_OpenText
                                    : c == '>' ? addr::cGZUIParser_CloseText
                                               : addr::cGZUIParser_EqualsText;
        Virtual<uint32_t>(*out, 0x10, reinterpret_cast<const char*>(text));
        *type = c == '<' ? kOpen : c == '>' ? kClose : kEquals;
        ++cursor;
        return Answer(Bits(type), true);
    }
    const char* const delimiters = reinterpret_cast<const char*>(addr::cGZUIParser_Delimiters);
    while (!CallCdecl<const char*>(addr::SC4_StrChr, delimiters, static_cast<int32_t>(*cursor))) {
        ++cursor;
        if (!*cursor)
            break;
    }
    Virtual<uint32_t>(*out, 0xC, start, static_cast<uint32_t>(cursor - start));
    *type = kWord;
    return 1;
}

// The game's 0x00944FB6: the text at +0x28 parsed (a parse already running
// is refused). Tags and attribute names are numbers (variables or digits);
// a tag whose name is not one is skipped to its `>`, an attribute whose
// name is not one is skipped with its value. A token out of place stops it
// as failed. The token read when the state turns 9 or 10 is not released,
// as in the game.
uint32_t __fastcall ParserParse(char* self, void*)
{
    OPENSC4_FIRST("ParserParse");
    uint32_t& state = At<uint32_t>(self, kState);
    if (state != 0)
        return 0;
    At<char*>(self, kCursor) = At<char*>(self, kBuffer);
    state = kOutside;
    ParserStartDocument(self, nullptr);
    void* token = nullptr;
    int32_t type = 0;
    if (Yes(ParserNextToken(self, nullptr, &token, &type, state == kOutside))) {
        for (;;) {
            if (state == kFailed || state == kStop)
                break;
            switch (state) {
            case kOutside:
                if (type < 0)
                    state = kFailed;
                else if (type <= kWord)
                    ParserText(self, nullptr, token);
                else if (type == kOpen)
                    state = kTagName;
                else
                    state = kFailed;
                break;
            case kTagName:
                if (type == kWord)
                    state = Yes(ParserBeginTag(self, nullptr, token)) ? kInside : kSkipTag;
                else if (type == kClose)
                    state = kOutside;
                else
                    state = kFailed;
                break;
            case kInside:
            case kAfterName:
                if (type == kWord) {
                    ParserCommitAttribute(self, nullptr);
                    state = Yes(ParserSetAttributeName(self, nullptr, token)) ? kAfterName : kSkipName;
                } else if (type == kClose) {
                    ParserCommitAttribute(self, nullptr);
                    ParserFinishTag(self, nullptr);
                    state = kOutside;
                } else if (type == kEquals && state == kAfterName) {
                    state = kAfterEquals;
                } else {
                    state = kFailed;
                }
                break;
            case kAfterEquals:
                if (type == kText) {
                    ParserMakeStringValue(self, nullptr, token);
                    state = kInside;
                } else if (type == kWord) {
                    ParserMakeValue(self, nullptr, token);
                    state = kInside;
                } else {
                    state = kFailed;
                }
                break;
            case kSkipTag:
                if (type == kClose)
                    state = kOutside;
                break;
            case kSkipName:
                if (type == kWord) {
                    state = Yes(ParserSetAttributeName(self, nullptr, token)) ? kAfterName : kSkipName;
                } else if (type == kClose) {
                    ParserFinishTag(self, nullptr);
                    state = kOutside;
                } else if (type == kEquals) {
                    state = kSkipValue;
                } else {
                    state = kFailed;
                }
                break;
            case kSkipValue:
                state = type == kWord || type == kText ? kInside : kFailed;
                break;
            default:
                break;
            }
            Release(token);
            token = nullptr;
            if (!Yes(ParserNextToken(self, nullptr, &token, &type, state == kOutside)))
                break;
        }
    }
    At<char*>(self, kCursor) = nullptr;
    const uint32_t eax = state == kFailed ? ParserEndFailed(self, nullptr) : ParserEndDone(self, nullptr);
    const bool done = state != kFailed;
    state = 0;
    return Answer(eax, done);
}

// The game's 0x00945229 (slot 6): a game string's text.
uint32_t __fastcall ParserParseString(char* self, void*, void* text)
{
    OPENSC4_FIRST("ParserParseString");
    At<const char*>(self, kBuffer) = Virtual<const char*>(text, 0x18);
    return ParserParse(self, nullptr);
}

// The game's 0x00945198 (slot 7): a file read whole. The buffer is freed
// and +0x28 left pointing at it, as in the game.
uint32_t __fastcall ParserParseFile(char* self, void*, void* path)
{
    OPENSC4_FIRST("ParserParseFile");
    alignas(4) char file[0x80];
    CallThis<void*>(addr::cRZFile_Construct, file, path);
    bool done = false;
    if (Yes(CallThis<uint32_t>(addr::cRZFile_Exists, file))) {
        CallThis<uint32_t>(addr::cRZFile_Open, file, uint32_t{1}, uint32_t{2}, uint32_t{1});
        if (CallThis<uint32_t>(addr::cRZFile_Length, file)) {
            uint32_t length = CallThis<uint32_t>(addr::cRZFile_Length, file);
            char* const buffer = static_cast<char*>(New(length + 1));
            At<char*>(self, kBuffer) = buffer;
            CallThis<uint32_t>(addr::cRZFile_ReadLength, file, buffer, &length);
            At<char*>(self, kBuffer)[length] = 0;
            done = Yes(ParserParse(self, nullptr));
            Delete(At<char*>(self, kBuffer));
        }
    }
    return Answer(CallThis<uint32_t>(addr::cRZFile_Destruct, file), done);
}

// The game's 0x00944C72 (slot 8): a resource by its key, read whole
// through its seekable interface into a game string and parsed by slot 6.
uint32_t __fastcall ParserParseResource(char* self, void*, const void* key)
{
    OPENSC4_FIRST("ParserParseResource");
    void* manager = nullptr;
    CallThis<void*>(addr::SC4RefPtr_ResourceManager, &manager);
    void* resource = nullptr;
    bool done = false;
    if (Yes(Virtual<uint32_t>(manager, 0x94, key, &resource, uint32_t{0}))) {
        void* seekable = nullptr;
        done = Yes(Virtual<uint32_t>(resource, 0x0, kIID_Seekable, &seekable));
        if (done) {
            const uint32_t size = Virtual<uint32_t>(seekable, 0xD0);
            void* const buffer = New(size);
            Virtual<uint32_t>(seekable, 0x6C, buffer, size);
            Release(seekable);
            seekable = nullptr;
            TempString text;
            CallThis<void*>(addr::SC4String_FromBytes, &text, buffer, size);
            Delete(buffer);
            done = Yes(Virtual<uint32_t>(self, 0x18, &text));
            At<std::uintptr_t>(&text, 0) = addr::SC4String_Vtable;
            if (Body(text))
                Free(Body(text));
        }
        Virtual<uint32_t>(manager, 0x98, key, &resource);
    }
    return Answer(CallThis<uint32_t>(addr::SC4RefPtr_Destruct, &manager), done);
}

// ====================================================================================
// The parser 0xBA2E7953 (vtable 0x00AD4718, 0x5C bytes)

// The game's 0x0094451C (slot 3): the listener, and its two event
// interfaces asked for; null lets them go.
uint32_t __fastcall ParserSetListener(char* self, void*, void* listener)
{
    OPENSC4_FIRST("ParserSetListener");
    uint32_t eax = 0;
    for (std::size_t offset : {kListener, kTagEvents, kAttributeEvents}) {
        if (void* const held = At<void*>(self, offset)) {
            eax = Release(held);
            At<void*>(self, offset) = nullptr;
        }
    }
    if (listener) {
        AddRef(listener);
        At<void*>(self, kListener) = listener;
        Virtual<uint32_t>(listener, 0x0, kIID_TagEvents, &At<void*>(self, kTagEvents));
        eax = Virtual<uint32_t>(At<void*>(self, kListener), 0x0, kIID_AttributeEvents,
                                &At<void*>(self, kAttributeEvents));
    }
    return Answer(eax, true);
}

// The game's 0x00944589 (slot 4): an object held at +0x20, which nothing
// reads and the destructor does not let go.
uint32_t __fastcall ParserSetContext(char* self, void*, void* context)
{
    OPENSC4_FIRST("ParserSetContext");
    uint32_t eax = 0;
    if (void* const held = At<void*>(self, kContext)) {
        eax = Release(held);
        At<void*>(self, kContext) = nullptr;
    }
    At<void*>(self, kContext) = context;
    if (context)
        eax = AddRef(context);
    return Answer(eax, true);
}

// The game's 0x00944650. A refusal leaves iid - 0x5A2C1570 above AL.
uint32_t __fastcall ParserQueryInterface(char* self, void*, uint32_t iid, void** out)
{
    OPENSC4_FIRST("ParserQueryInterface");
    if (iid != kIID_cIGZUnknown && iid != kIID_Parser)
        return Answer(iid - kIID_Parser, false);
    *out = self;
    return Answer(AddRef(self), true);
}

// The game's 0x00945404: one variable table (0xBA2E7954) on the stack.
char* __fastcall ParserConstruct(char* self, void*)
{
    OPENSC4_FIRST("ParserConstruct");
    At<std::uintptr_t>(self, 0) = addr::cGZUIParser_Vtable;
    At<uint32_t>(self, 0x4) = 0;
    At<void*>(self, kStack) = nullptr;
    At<void*>(self, kStack + 4) = nullptr;
    At<void*>(self, kStack + 8) = nullptr;
    At<void*>(self, kContext) = nullptr;
    CallThis<void*>(addr::SC4AttrVector_Construct, self + kAttributes);
    At<uint8_t>(self, kHaveName) = 0;
    At<uint32_t>(self, kName) = 0;
    At<void*>(self, kValue) = nullptr;
    At<uint32_t>(self, kState) = 0;
    At<void*>(self, kCursor) = nullptr;
    At<void*>(self, kBuffer) = nullptr;
    At<void*>(self, kListener) = nullptr;
    At<void*>(self, kTagEvents) = nullptr;
    At<void*>(self, kAttributeEvents) = nullptr;
    char* const memory = static_cast<char*>(New(0x14));
    void* table = memory ? VarsConstruct(memory, nullptr) : nullptr;
    AddRef(table);
    CallThis<uint32_t>(addr::SC4PtrVector_PushBack8FF785, self + kStack, &table);
    return self;
}

// The game's 0x00944462.
void* __fastcall ParserNew(void*, void*)
{
    OPENSC4_FIRST("ParserNew");
    char* const memory = static_cast<char*>(New(0x5C));
    return memory ? ParserConstruct(memory, nullptr) : nullptr;
}

// The game's 0x00944B7E. The object at +0x20 is not let go.
uint32_t __fastcall ParserDestruct(char* self, void*)
{
    OPENSC4_FIRST("ParserDestruct");
    At<std::uintptr_t>(self, 0) = addr::cGZUIParser_Vtable;
    if (void* const value = At<void*>(self, kValue)) {
        Release(value);
        At<void*>(self, kValue) = nullptr;
    }
    ParserSetListener(self, nullptr, nullptr);
    while (Yes(ParserPopVars(self, nullptr))) {
    }
    Release(Top(self));
    CallThis<uint32_t>(addr::SC4PtrVector_EraseRange, self + kStack, At<void*>(self, kStack),
                       At<void*>(self, kStack + 4));
    uint32_t eax = CallThis<uint32_t>(addr::SC4AttrVector_Destruct, self + kAttributes);
    if (void* const begin = At<void*>(self, kStack))
        eax = Free(begin);
    return eax;
}

// The game's 0x00944C2F (slot 15).
char* __fastcall ParserDeletingDestructor(char* self, void*, uint32_t flags)
{
    OPENSC4_FIRST("ParserDeletingDestructor");
    ParserDestruct(self, nullptr);
    if (flags & 1)
        Delete(self);
    return self;
}

// ====================================================================================
// The directors

// The game's 0x0094448E: the parser's director (vtables 0x00AD46C8 and
// 0x00AD469C), the parser and the variable table registered by their
// factories' addresses.
char* __fastcall DirectorConstruct(char* self, void*)
{
    OPENSC4_FIRST("DirectorConstruct");
    CallThis<void*>(addr::cRZCOMDllDirector_Construct, self);
    At<std::uintptr_t>(self, 0) = addr::cGZUIParserDirector_Vtable;
    At<std::uintptr_t>(self, 0x4) = addr::cGZUIParserDirector_Vtable2;
    CallThis<void>(addr::cRZCOMDllDirector_AddCls, self, kClsid_Parser, addr::cGZUIParser_New);
    CallThis<void>(addr::cRZCOMDllDirector_AddCls, self, kClsid_Vars, addr::cGZUIParserVars_New);
    return self;
}

// The game's 0x009444CF: the static director, made on first use.
void* __fastcall DirectorGet(void*, void*)
{
    OPENSC4_FIRST("DirectorGet");
    uint32_t& guard = *reinterpret_cast<uint32_t*>(addr::cGZUIParserDirector_Guard);
    char* const director = reinterpret_cast<char*>(addr::cGZUIParserDirector_Instance);
    if (!(guard & 1)) {
        guard |= 1;
        DirectorConstruct(director, nullptr);
        CallCdecl<int32_t>(addr::SC4_atexit, addr::cGZUIParserDirector_AtExit);
    }
    return director;
}

// The game's 0x00945636: the next director's (vtables 0x00AD47C0 and
// 0x00AD4790, part u5).
char* __fastcall Director2DeletingDestructor(char* self, void*, uint32_t flags)
{
    OPENSC4_FIRST("Director2DeletingDestructor");
    At<std::uintptr_t>(self, 0) = addr::cGZUIDirector2_Vtable;
    At<std::uintptr_t>(self, 0x4) = addr::cGZUIDirector2_Vtable2;
    CallThis<void*>(addr::cRZCOMDllDirector_Destruct, self);
    if (flags & 1)
        Delete(self);
    return self;
}

// Its factories: an object of the size made by its constructor (the
// first three answer it, the last two its interface at +4).
void* Make(uint32_t size, std::uintptr_t construct)
{
    void* const memory = New(size);
    return memory ? CallThis<void*>(construct, memory) : nullptr;
}

void* AtFour(void* object) { return object ? static_cast<char*>(object) + 4 : nullptr; }

void* __fastcall Director2New68(void*, void*)
{
    OPENSC4_FIRST("Director2New68");
    return Make(0x68, addr::cGZUIDirector2_Construct68);
}

void* __fastcall Director2New4048(void*, void*)
{
    OPENSC4_FIRST("Director2New4048");
    return Make(0x4048, addr::cGZUIDirector2_Construct4048);
}

void* __fastcall Director2New5C(void*, void*)
{
    OPENSC4_FIRST("Director2New5C");
    return Make(0x5C, addr::cGZUIDirector2_Construct5C);
}

void* __fastcall Director2New4144(void*, void*)
{
    OPENSC4_FIRST("Director2New4144");
    return AtFour(Make(0x4144, addr::cGZUIDirector2_Construct4144));
}

void* __fastcall Director2New190(void*, void*)
{
    OPENSC4_FIRST("Director2New190");
    return AtFour(Make(0x190, addr::cGZUIDirector2_Construct190));
}

// ====================================================================================
// Thunks and installation

OPENSC4_THUNK(VarsNewThunk, VarsNew, 0)
OPENSC4_THUNK(VarsConstructThunk, VarsConstruct, 0)
OPENSC4_THUNK(VarsDestructThunk, VarsDestruct, 0)
OPENSC4_THUNK(VarsDeletingDestructorThunk, VarsDeletingDestructor, 4)
OPENSC4_THUNK(VarsQueryInterfaceThunk, VarsQueryInterface, 8)
OPENSC4_THUNK(VarsSetNumberThunk, VarsSetNumber, 8)
OPENSC4_THUNK(VarsSetThunk, VarsSet, 8)
OPENSC4_THUNK(VarsRemoveThunk, VarsRemove, 4)
OPENSC4_THUNK(VarsGetThunk, VarsGet, 8)
OPENSC4_THUNK(VarsClearThunk, VarsClear, 0)

OPENSC4_THUNK(ParserPushVarsThunk, ParserPushVars, 4)
OPENSC4_THUNK(ParserPopVarsThunk, ParserPopVars, 0)
OPENSC4_THUNK(ParserLookupThunk, ParserLookup, 8)
OPENSC4_THUNK(ParserSetNumberThunk, ParserSetNumber, 8)
OPENSC4_THUNK(ParserSetVarThunk, ParserSetVar, 8)
OPENSC4_THUNK(ParserRemoveVarThunk, ParserRemoveVar, 4)
OPENSC4_THUNK(ParserClearVarsThunk, ParserClearVars, 0)

OPENSC4_THUNK(ParserResolveNumberThunk, ParserResolveNumber, 8)
OPENSC4_THUNK(ParserStartDocumentThunk, ParserStartDocument, 0)
OPENSC4_THUNK(ParserTextThunk, ParserText, 4)
OPENSC4_THUNK(ParserClearAttributesThunk, ParserClearAttributes, 0)
OPENSC4_THUNK(ParserEndFailedThunk, ParserEndFailed, 0)
OPENSC4_THUNK(ParserEndDoneThunk, ParserEndDone, 0)
OPENSC4_THUNK(ParserBeginTagThunk, ParserBeginTag, 4)
OPENSC4_THUNK(ParserSetAttributeNameThunk, ParserSetAttributeName, 4)
OPENSC4_THUNK(ParserMakeStringValueThunk, ParserMakeStringValue, 4)
OPENSC4_THUNK(ParserMakeValueThunk, ParserMakeValue, 4)
OPENSC4_THUNK(ParserCommitAttributeThunk, ParserCommitAttribute, 0)
OPENSC4_THUNK(ParserFinishTagThunk, ParserFinishTag, 0)
OPENSC4_THUNK(ParserNextTokenThunk, ParserNextToken, 12)
OPENSC4_THUNK(ParserParseThunk, ParserParse, 0)

OPENSC4_THUNK(ParserParseStringThunk, ParserParseString, 4)
OPENSC4_THUNK(ParserParseFileThunk, ParserParseFile, 4)
OPENSC4_THUNK(ParserParseResourceThunk, ParserParseResource, 4)
OPENSC4_THUNK(ParserSetListenerThunk, ParserSetListener, 4)
OPENSC4_THUNK(ParserSetContextThunk, ParserSetContext, 4)
OPENSC4_THUNK(ParserQueryInterfaceThunk, ParserQueryInterface, 8)
OPENSC4_THUNK(ParserConstructThunk, ParserConstruct, 0)
OPENSC4_THUNK(ParserNewThunk, ParserNew, 0)
OPENSC4_THUNK(ParserDestructThunk, ParserDestruct, 0)
OPENSC4_THUNK(ParserDeletingDestructorThunk, ParserDeletingDestructor, 4)

OPENSC4_THUNK(DirectorConstructThunk, DirectorConstruct, 0)
OPENSC4_THUNK(DirectorGetThunk, DirectorGet, 0)
OPENSC4_THUNK(Director2DeletingDestructorThunk, Director2DeletingDestructor, 4)
OPENSC4_THUNK(Director2New68Thunk, Director2New68, 0)
OPENSC4_THUNK(Director2New4048Thunk, Director2New4048, 0)
OPENSC4_THUNK(Director2New5CThunk, Director2New5C, 0)
OPENSC4_THUNK(Director2New4144Thunk, Director2New4144, 0)
OPENSC4_THUNK(Director2New190Thunk, Director2New190, 0)

struct Detour {
    std::uintptr_t address;
    void (*ours)();
};

template <std::size_t N>
bool InstallAll(const Detour (&detours)[N])
{
    if (!game::IsBuild641())
        return false;
    for (const Detour& detour : detours) {
        const addr::Entry* entry = addr_check::Find(detour.address);
        void* original = nullptr;
        if (!entry || !hook::Install(*entry, reinterpret_cast<void*>(detour.ours), &original))
            return false;
    }
    return true;
}

bool InstallParser(registry::Mode)
{
    const Detour detours[] = {
        {addr::cGZUIParser_New, &ParserNewThunk},
        {addr::cGZUIParser_Construct, &ParserConstructThunk},
        {addr::cGZUIParser_Destruct, &ParserDestructThunk},
        {addr::cGZUIParser_DeletingDestructor, &ParserDeletingDestructorThunk},
        {addr::cGZUIParser_QueryInterface, &ParserQueryInterfaceThunk},
        {addr::cGZUIParser_SetListener, &ParserSetListenerThunk},
        {addr::cGZUIParser_SetContext, &ParserSetContextThunk},
        {addr::cGZUIParser_ParseString, &ParserParseStringThunk},
        {addr::cGZUIParser_ParseFile, &ParserParseFileThunk},
        {addr::cGZUIParser_ParseResource, &ParserParseResourceThunk},
    };
    return InstallAll(detours);
}

bool InstallTokens(registry::Mode)
{
    const Detour detours[] = {
        {addr::cGZUIParser_Parse, &ParserParseThunk},
        {addr::cGZUIParser_NextToken, &ParserNextTokenThunk},
        {addr::cGZUIParser_StartDocument, &ParserStartDocumentThunk},
        {addr::cGZUIParser_EndFailed, &ParserEndFailedThunk},
        {addr::cGZUIParser_EndDone, &ParserEndDoneThunk},
        {addr::cGZUIParser_Text, &ParserTextThunk},
        {addr::cGZUIParser_BeginTag, &ParserBeginTagThunk},
        {addr::cGZUIParser_FinishTag, &ParserFinishTagThunk},
        {addr::cGZUIParser_SetAttributeName, &ParserSetAttributeNameThunk},
        {addr::cGZUIParser_MakeStringValue, &ParserMakeStringValueThunk},
        {addr::cGZUIParser_MakeValue, &ParserMakeValueThunk},
        {addr::cGZUIParser_CommitAttribute, &ParserCommitAttributeThunk},
        {addr::cGZUIParser_ClearAttributes, &ParserClearAttributesThunk},
        {addr::cGZUIParser_ResolveNumber, &ParserResolveNumberThunk},
    };
    return InstallAll(detours);
}

bool InstallStack(registry::Mode)
{
    const Detour detours[] = {
        {addr::cGZUIParser_PushVars, &ParserPushVarsThunk},
        {addr::cGZUIParser_PopVars, &ParserPopVarsThunk},
        {addr::cGZUIParser_Lookup, &ParserLookupThunk},
        {addr::cGZUIParser_SetNumber, &ParserSetNumberThunk},
        {addr::cGZUIParser_SetVar, &ParserSetVarThunk},
        {addr::cGZUIParser_RemoveVar, &ParserRemoveVarThunk},
        {addr::cGZUIParser_ClearVars, &ParserClearVarsThunk},
    };
    return InstallAll(detours);
}

bool InstallTable(registry::Mode)
{
    const Detour detours[] = {
        {addr::cGZUIParserVars_New, &VarsNewThunk},
        {addr::cGZUIParserVars_Construct, &VarsConstructThunk},
        {addr::cGZUIParserVars_Destruct, &VarsDestructThunk},
        {addr::cGZUIParserVars_DeletingDestructor, &VarsDeletingDestructorThunk},
        {addr::cGZUIParserVars_QueryInterface, &VarsQueryInterfaceThunk},
        {addr::cGZUIParserVars_SetNumber, &VarsSetNumberThunk},
        {addr::cGZUIParserVars_Set, &VarsSetThunk},
        {addr::cGZUIParserVars_Remove, &VarsRemoveThunk},
        {addr::cGZUIParserVars_Get, &VarsGetThunk},
        {addr::cGZUIParserVars_Clear, &VarsClearThunk},
    };
    return InstallAll(detours);
}

bool InstallDirector(registry::Mode)
{
    const Detour detours[] = {
        {addr::cGZUIParserDirector_Construct, &DirectorConstructThunk},
        {addr::cGZUIParserDirector_Get, &DirectorGetThunk},
        {addr::cGZUIDirector2_DeletingDestructor, &Director2DeletingDestructorThunk},
        {addr::cGZUIDirector2_New68, &Director2New68Thunk},
        {addr::cGZUIDirector2_New4048, &Director2New4048Thunk},
        {addr::cGZUIDirector2_New5C, &Director2New5CThunk},
        {addr::cGZUIDirector2_New4144, &Director2New4144Thunk},
        {addr::cGZUIDirector2_New190, &Director2New190Thunk},
    };
    return InstallAll(detours);
}

const bool g_registered[] = {
    registry::Register({"framework.xmlparse.parser", "framework/xmlparse", registry::Kind::Detour,
                        registry::Phase::Attach, addr::cGZUIParser_ParseString,
                        "Our UI file parser 0xBA2E7953: the factory, constructor, destructors, QueryInterface, "
                        "SetListener, SetContext and the string, file and resource parses (10 detours; part u4, "
                        "docs/gzui.md)",
                        false, InstallParser}),
    registry::Register({"framework.xmlparse.tokens", "framework/xmlparse", registry::Kind::Detour,
                        registry::Phase::Attach, addr::cGZUIParser_Parse,
                        "Our UI file parser's parse: the state machine, the tokenizer and its steps (14 detours; part "
                        "u4, docs/gzui.md)",
                        false, InstallTokens}),
    registry::Register({"framework.xmlparse.stack", "framework/xmlparse", registry::Kind::Detour,
                        registry::Phase::Attach, addr::cGZUIParser_Lookup,
                        "Our UI file parser's stack of variable tables: push, pop, lookup and slots 9 to 12 (7 "
                        "detours; part u4, docs/gzui.md)",
                        false, InstallStack}),
    registry::Register({"framework.xmlparse.table", "framework/xmlparse", registry::Kind::Detour,
                        registry::Phase::Attach, addr::cGZUIParserVars_Get,
                        "Our UI file parser's variable table 0xBA2E7954: the factory, constructor, destructors, "
                        "QueryInterface, SetNumber, Set, Remove, Get, Clear (10 detours; part u4, docs/gzui.md)",
                        false, InstallTable}),
    registry::Register({"framework.xmlparse.director", "framework/xmlparse", registry::Kind::Detour,
                        registry::Phase::Attach, addr::cGZUIParserDirector_Construct,
                        "Our UI file parser's director (constructor, the static one's getter) and the next "
                        "director's deleting destructor and five factories (8 detours; part u4, docs/gzui.md)",
                        false, InstallDirector}),
};

} // namespace
} // namespace opensc4::gzui
