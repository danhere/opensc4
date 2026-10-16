// The query tool of the city view, seventh piece, second half: the hover
// tips of a building and of a cell and the texts of a lot's map levels,
// 0x004D4F90 to 0x004D6C60 and 0x004D6C70 to 0x004D711A (jump tables at
// 0x004D5C6C / 0x004D5C78 and 0x004D7120 stay in the exe).
// Replacement, off by default:
//
//   ui.tools.query7b: 5 functions of the query tool
//     (cSC4ViewInputControlQuery: a view input control of 0xA0 bytes, vtable
//     0x00A90A88; docs/tools.md): the insert into the query's text map
//     (0x004D4F90), the texts of a lot's map levels and states put into
//     such a map by number (0x004D4FD0), the three highest of them appended
//     to a text (0x004D5C90), the tip of a building (0x004D5E30: title, text,
//     background and meter pictures, meter share) and the tip of a cell
//     without an occupant (0x004D6C70: title and text by zone, the lot's
//     level texts).
//
// The text map is an STLport multimap of a number and a game string: a
// header node of 0x28 bytes from the STL allocator (colour byte, parent,
// leftmost, rightmost) and a count; nodes of 0x28 bytes (left +0x8, right
// +0xC, the number +0x10, the string +0x14). Equal numbers go after the
// ones already there. The walks take the entries from the highest number
// down, at most three.
//
// sc4-query-ui-hooks patches the call to 0x004D5E30 at 0x004D7464 (inside
// DoMessage, translator A's) and calls 0x004D5E30 by address
// (RealGetBuildingOccupantTipInfo): with ours at the entry the mod reaches
// ours. No mod writes inside these five functions.
//
// Nothing here changes the city: getters, strings, a map made and freed.
// No random number is drawn and no clock is read; 0x004D5E30 asks whether
// the debug keys are down (0x004C4700: shift, control and alt held and the
// application's slot 21), the keyboard as it is.
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"
#include "core/tool_query.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>

namespace opensc4::tool_query_7b {
namespace {

namespace field = tool_query::field;
namespace text_node = tool_query::text_node;

constexpr uint32_t kQueryTextGroup = 0xEA5524EB;   // the group of the query's game strings
constexpr uint32_t kBuildingIid = 0x87DDA3A9;      // the building occupant's interface
constexpr uint32_t kPurposesProperty = 0xAA1DD396; // a building's purposes (uint32 array)
constexpr uint32_t kValueIid = 0x60FD4D2B;         // the property's value
constexpr uint32_t kUint32Array = 0x8006;          // the value's type
constexpr uint32_t kNameProperty = 0x20;           // the debug title: the exemplar's name
constexpr uint32_t kLotProperty = 0x8999B20B;      // a uint32 of the lot's: a text when not 0

// The text map's header and node layout (see the top).
constexpr std::size_t kNodeColour = 0x0;
constexpr std::size_t kNodeParent = 0x4;
constexpr std::size_t kNodeSize = 0x28;
constexpr std::size_t kNodeTextEnd = 0x1C;

// The line end the walks put between texts (the game's "\n" at 0x00A916BC).
constexpr char kNewLine[] = "\n";

template <typename R, typename... Args>
R This(std::uintptr_t function, void* self, Args... args)
{
    return reinterpret_cast<R(__thiscall*)(void*, Args...)>(function)(self, args...);
}

template <typename R, typename... Args>
R Cdecl(std::uintptr_t function, Args... args)
{
    return reinterpret_cast<R(__cdecl*)(Args...)>(function)(args...);
}

void Release(void* object)
{
    Virtual<uint32_t>(object, 0x8);
}

// __ftol (0x009EEF04): truncation toward zero into 64 bits; what is out of
// range or not a number gives 0x8000000000000000, as the x87 does.
int64_t Ftol64(double value)
{
    if (!(value > -9223372036854775808.0 && value < 9223372036854775808.0))
        return INT64_MIN;
    return static_cast<int64_t>(value);
}

// The low 32 bits of the same.
int32_t Ftol(double value)
{
    return static_cast<int32_t>(Ftol64(value));
}

// --- The game's strings --------------------------------------------------------------------

// SC4String as the game keeps one on the stack: the vtable 0x00A80810, an
// STL body (begin, end, end of the buffer) and a zero dword.
struct GameString {
    std::uintptr_t vtable;
    char* begin;
    char* end;
    char* capacity;
    uint32_t zero;
};
static_assert(sizeof(GameString) == 0x14);

// SC4LocalizedString: the string, then the key. 0x20 bytes.
struct LocalizedString {
    GameString text;
    uint32_t key[3];
};
static_assert(sizeof(LocalizedString) == 0x20);

// A pair of the text map (tool_query::text_entry): the number, the string.
struct TextPair {
    int32_t key;
    GameString text;
};
static_assert(sizeof(TextPair) == tool_query::text_entry::kSize);

// The text map as the callers keep it on the stack: the header node and the
// count (a third dword the game never touches).
struct TextMap {
    char* header;
    uint32_t count;
    uint32_t unused;
};

// The game's inline destructor (SC4String::Destroy 0x00414250 does the
// same): the vtable set to the string's again, the buffer freed.
void Destroy(GameString& text)
{
    text.vtable = addr::SC4String_Vtable;
    if (text.begin)
        Cdecl<void>(addr::SC4_Free, static_cast<void*>(text.begin));
}

// An empty string as the game makes one in line: 8 bytes from the STL
// allocator, the first cleared.
void MakeEmpty(GameString& text)
{
    char* const buffer = Cdecl<char*>(addr::SC4_stl_allocate, uint32_t{8});
    text.begin = buffer;
    text.end = buffer;
    text.capacity = buffer + 8;
    *buffer = 0;
    text.vtable = addr::SC4String_Vtable;
    text.zero = 0;
}

// A localized string of the query's group, loaded (0x00603580 with 1).
void MakeLocalized(LocalizedString& text, uint32_t instance)
{
    This<void*>(addr::SC4LocalizedString_SC4LocalizedString, &text, kQueryTextGroup, instance, uint32_t{1});
}

// The body of an SC4String the caller passed (its +4).
char* BodyOf(void* text)
{
    return static_cast<char*>(text) + 0x4;
}

bool BodyEmpty(char* body)
{
    return At<char*>(body, 0x0) == At<char*>(body, 0x4);
}

// The game string into `text` through Assign (0x00424380); the localized
// string is then destroyed by SC4String::Destroy (0x00414250), as the
// game does in these functions.
void SetText(void* text, uint32_t instance)
{
    LocalizedString made;
    MakeLocalized(made, instance);
    This<void>(addr::cRZBaseString_Assign, text, &made);
    This<void>(addr::SC4String_Destroy, &made);
}

// --- The text map ---------------------------------------------------------------------------

// A new empty map as both callers make it in line: the header node from
// the STL allocator, red, no root, leftmost and rightmost itself.
void MakeMap(TextMap& map)
{
    map.header = nullptr;
    char* const header = Cdecl<char*>(addr::SC4_stl_allocate, uint32_t{kNodeSize});
    map.header = header;
    map.count = 0;
    map.unused = 0;
    At<uint8_t>(header, kNodeColour) = 0;
    At<char*>(header, kNodeParent) = nullptr;
    At<char*>(header, text_node::kLeft) = header;
    At<char*>(header, text_node::kRight) = header;
}

// What both callers do at the end: the nodes freed (0x004CB8B0 from the
// root) when there are any, the header put back to empty, then freed.
void FreeMap(TextMap& map)
{
    if (map.count) {
        This<void>(addr::SC4QueryTool_EraseTexts, &map, At<char*>(map.header, kNodeParent));
        At<char*>(map.header, text_node::kLeft) = map.header;
        At<char*>(map.header, kNodeParent) = nullptr;
        At<char*>(map.header, text_node::kRight) = map.header;
        map.count = 0;
    }
    if (map.header)
        Cdecl<void>(addr::SC4_Free, static_cast<void*>(map.header));
}

// The tree iterator's step back (STLport's decrement, in line in
// 0x004D5C90; SC4TreeIterator::Decrement 0x006809E0 is the same): from the
// header to the rightmost node, else the rightmost of the left subtree,
// else up while coming from the left.
char* Previous(char* node)
{
    if (At<uint8_t>(node, kNodeColour) == 0 && At<char*>(At<char*>(node, kNodeParent), kNodeParent) == node)
        return At<char*>(node, text_node::kRight);
    if (char* left = At<char*>(node, text_node::kLeft)) {
        while (At<char*>(left, text_node::kRight))
            left = At<char*>(left, text_node::kRight);
        return left;
    }
    char* parent = At<char*>(node, kNodeParent);
    while (node == At<char*>(parent, text_node::kLeft)) {
        node = parent;
        parent = At<char*>(parent, kNodeParent);
    }
    return parent;
}

// The text of a node appended to a body (0x004081D0), after a line end
// (0x0040EBC0 with the game's "\n") when the body is not empty.
void AppendLine(char* body, char* node, void* tag)
{
    This<void>(addr::SC4String_AppendRange, body, At<char*>(node, text_node::kTextBuffer),
               At<char*>(node, kNodeTextEnd), tag);
}

void AppendNewLine(char* body, void* tag)
{
    if (!BodyEmpty(body))
        This<void>(addr::cSC4Advisor_F40EBC0, body, &kNewLine[0], &kNewLine[1], tag);
}

// How a function of this piece makes the pair it puts into the map; the
// three ways the game has (the effects are alike, the calls differ):
// TextPairCopy 0x004CCF20 and both strings destroyed in line; TextPairCopy
// and the pair and string destroyed by 0x004C6AF0 and 0x00414250; or the
// pair built in line (CopyBuffer 0x004089B0 into its body) and both
// destroyed in line.
enum class Made { Copied, CopiedByCalls, InLine };

// A game string of the query's group put into the map under a number
// (0x004D4F90; the place it went to is not looked at).
void AddText(TextMap* map, int32_t key, uint32_t instance, Made how)
{
    LocalizedString text;
    MakeLocalized(text, instance);
    TextPair pair;
    if (how == Made::InLine) {
        pair.key = key;
        uint8_t tag = 0;
        This<void>(addr::SC4String_CopyBuffer, static_cast<void*>(&pair.text.begin),
                   static_cast<void*>(&text.text.begin), uint32_t{0}, uint32_t{0xFFFFFFFF}, static_cast<void*>(&tag));
        pair.text.vtable = addr::SC4String_Vtable;
        pair.text.zero = 0;
    } else {
        This<void*>(addr::SC4QueryTool_TextPairCopy, &pair, &key, &text);
    }
    void* place = nullptr;
    This<void*>(addr::SC4QueryTool_InsertText, map, &place, &pair);
    if (how == Made::CopiedByCalls) {
        This<void>(addr::SC4QueryTool_DestroyTextEntry, &pair);
        This<void>(addr::SC4String_Destroy, &text);
    } else {
        Destroy(pair.text);
        Destroy(text.text);
    }
}

// --- 0x004D4F90 ------------------------------------------------------------------------------

// The game's 0x004D4F90 (thiscall (place out, pair), ret 8; this is the
// map; caller 0x004D4FD0): the pair put into the map after the entries of
// its number or lower (signed): down from the root, left when the number
// is below the node's, else right; then the tree's insert at the leaf
// found (0x004B6490: place out, child, parent, pair, 0). The place out.
void** __fastcall InsertText(TextMap* map, void*, void** place, TextPair* pair)
{
    char* parent = map->header;
    char* node = At<char*>(parent, kNodeParent);
    while (node) {
        parent = node;
        node = pair->key < At<int32_t>(node, 0x10) ? At<char*>(node, text_node::kLeft)
                                                    : At<char*>(node, text_node::kRight);
    }
    This<void*>(addr::SC4TextMultimap_InsertNode, map, place, node, parent, pair, uint32_t{0});
    return place;
}

// --- 0x004D4FD0 ------------------------------------------------------------------------------

// The game's 0x004D4FD0 (cdecl (lot, map), plain ret; callers 0x004D5C90
// and 0x004D6C70): what the lot's growth developer (0x004D32D0) makes of
// its cell, as game strings put into the map under numbers. Asked first,
// in this order: the lot's needs power (slot 65), its centre (slot 15) and
// cell (city slot 95), its building type (slot 46 with 0), the developer
// and the zone at the cell (0x00B43D14 slot 12); nothing more without a
// developer. With a building type the wealth it is built for (0x004D3260),
// else 0 and power is taken as needed.
//   - the uint32 property 0x8999B20B of the lot, when there and not 0:
//     0x0A5BBA2A under 90;
//   - power needed and not there (slot 66): 0x6A5BBBA0 under 100;
//   - the pollution and garbage levels (0x004C5C90, 0x004C5D70) at 2: a
//     string each under the level's number;
//   - the first three map levels (0x004C4750, 0x004C47F0, 0x004C4890) at 0
//     or 2: a string each under the number;
//   - for zones 1 to 9 (the jump table: residential 1-3, commercial 4-6,
//     industrial 7-9) the fourth and fifth levels (0x004C4930,
//     0x004C4A30) the same; a residential lot at the fifth level's 0 says
//     0xAA5BBC97 when it is vacant (slot 33) for being kicked out (slot
//     45), else 0xAA5BBC9F;
//   - the lot's state: constructing 0x4A5FBDC4, vacant 0x0A5FBDF2, under 0;
//   - the former occupants' wealth (slot 41) below the wealth built for
//     (signed): 0x6A5FBED8 under 1.
// The level functions are given the developer, the cell and a number out.
void __cdecl CollectLotTexts(void* lot, TextMap* map)
{
    uint8_t needsPower = Virtual<uint8_t>(lot, 0x104);
    float center[3] = {};
    Virtual<void*>(lot, 0x3C, static_cast<float*>(center));
    int32_t x = 0, z = 0;
    Virtual<void>(Global<void*>(addr::SC4_City), 0x17C, center[0], center[2], &x, &z);
    const uint32_t type = Virtual<uint32_t>(lot, 0xB8, uint32_t{0});
    int32_t wealth = 0;
    void* const developer = Cdecl<void*>(addr::SC4QueryTool_DeveloperOfLot, lot);
    const uint32_t zone = Virtual<uint32_t>(Global<void*>(addr::SC4_Service43D14), 0x30, x, z);
    if (!developer)
        return;
    if (type)
        wealth = Cdecl<int32_t>(addr::SC4QueryTool_LotWealth, lot);
    else
        needsPower = 1;

    uint32_t value = 0;
    void* const holder = Virtual<void*>(lot, 0xC);
    if (Cdecl<uint8_t>(addr::SC4_GetPropertyUint32, holder, kLotProperty, &value) && value != 0)
        AddText(map, 0x5A, 0x0A5BBA2A, Made::Copied);
    if (needsPower && !Virtual<uint8_t>(lot, 0x108))
        AddText(map, 0x64, 0x6A5BBBA0, Made::Copied);

    int32_t level = 0;
    const auto ask = [&](std::uintptr_t function) {
        return Cdecl<uint32_t>(function, developer, x, z, &level);
    };
    // A level at 0 or 2 gives a string; others nothing.
    const auto both = [&](std::uintptr_t function, uint32_t atZero, uint32_t atTwo, Made how) {
        const uint32_t answer = ask(function);
        if (answer == 0)
            AddText(map, level, atZero, how);
        else if (answer == 2)
            AddText(map, level, atTwo, how);
    };

    if (ask(addr::SC4QueryTool_LevelPollution) == 2)
        AddText(map, level, 0x8A5BBBF1, Made::InLine);
    if (ask(addr::SC4QueryTool_LevelGarbage) == 2)
        AddText(map, level, 0x0A82439F, Made::InLine);
    both(addr::SC4QueryTool_LevelFirst, 0xCA5BBC43, 0x2A5BBC39, Made::Copied);
    both(addr::SC4QueryTool_LevelSecond, 0x2A5BBC4D, 0x6A5BBC57, Made::Copied);
    both(addr::SC4QueryTool_LevelThird, 0x4A5BBC61, 0x6A5BBC73, Made::Copied);

    const uint32_t group = zone - 1;
    if (group <= 8) {
        if (group < 3) {
            both(addr::SC4QueryTool_LevelFourth, 0x2A5BBC91, 0x6A5BBC80, Made::CopiedByCalls);
            const uint32_t answer = ask(addr::SC4QueryTool_LevelFifth);
            if (answer == 0) {
                const bool kickedOut = Virtual<uint32_t>(lot, 0x84) == 4 && Virtual<uint32_t>(lot, 0xB4) == 4;
                AddText(map, level, kickedOut ? 0xAA5BBC97 : 0xAA5BBC9F, Made::CopiedByCalls);
            } else if (answer == 2) {
                AddText(map, level, 0xAA5BBC97, Made::CopiedByCalls);
            }
        } else if (group < 6) {
            both(addr::SC4QueryTool_LevelFourth, 0x6AA87825, 0x0AA87852, Made::CopiedByCalls);
            both(addr::SC4QueryTool_LevelFifth, 0xCAA8785C, 0x2AA8786B, Made::CopiedByCalls);
        } else {
            both(addr::SC4QueryTool_LevelFourth, 0x2A5BBC91, 0x6A5BBC80, Made::CopiedByCalls);
            both(addr::SC4QueryTool_LevelFifth, 0xCAA8785C, 0x2AA8786B, Made::CopiedByCalls);
        }
    }

    const uint32_t state = Virtual<uint32_t>(lot, 0x84);
    if (state == 2)
        AddText(map, 0, 0x4A5FBDC4, Made::Copied);
    else if (state == 4)
        AddText(map, 0, 0x0A5FBDF2, Made::Copied);
    if (static_cast<int32_t>(Virtual<uint32_t>(lot, 0xA4)) < wealth)
        AddText(map, 1, 0x6A5FBED8, Made::InLine);
}

// --- 0x004D5C90 ------------------------------------------------------------------------------

// The game's 0x004D5C90 (cdecl (occupant, text), plain ret; caller
// 0x004D5E30): the occupant's property holder is asked (slot 6, not used),
// then its lot (the lot manager's slot 14); for a lot the texts of
// 0x004D4FD0 in a map of its own, and the three of the highest numbers
// (the last put in first among equal ones) appended to the text, each
// after a line end when the text is not empty. The map is freed.
void __cdecl AppendLotTexts(void* occupant, void* text)
{
    TextMap map;
    MakeMap(map);
    Virtual<void*>(occupant, 0x18);
    if (void* const lot = Virtual<void*>(Global<void*>(addr::SC4_LotManager), 0x38, occupant))
        Cdecl<void>(addr::SC4QueryTool_CollectLotTexts, lot, &map);
    char* const body = BodyOf(text);
    uint8_t tag = 0;
    char* current = map.header;
    if (current != At<char*>(map.header, text_node::kLeft)) {
        for (uint32_t done = 0; done < 3;) {
            AppendNewLine(body, &tag);
            AppendLine(body, Previous(current), &tag);
            ++done;
            current = Previous(current);
            if (current == At<char*>(map.header, text_node::kLeft))
                break;
        }
    }
    FreeMap(map);
}

// --- 0x004D5E30 ------------------------------------------------------------------------------

// What the developer service (0x00B43D68) says the lot is built for: its
// building profile for the lot's building type (lot slot 46 with 0; service
// slot 39), then the developer type for the profile's purpose and the
// wealth (0x004D3260; service slot 30).
uint32_t DeveloperTypeOf(void* lot)
{
    void* const development = Global<void*>(addr::SC4_Service43D68);
    const uint32_t type = Virtual<uint32_t>(lot, 0xB8, uint32_t{0});
    const uint32_t* const profile = Virtual<const uint32_t*>(development, 0x9C, type);
    const uint32_t wealth = Cdecl<uint32_t>(addr::SC4QueryTool_LotWealth, lot);
    return Virtual<uint32_t>(development, 0x78, profile[0], wealth);
}

// A developer type's text, from a table of (type, game string).
struct TypeText {
    uint32_t type;
    uint32_t instance;
};
constexpr TypeText kResidentialTexts[] = {{0x1010, 0xCA95F27E}, {0x1020, 0x6A95F2B0}, {0x1030, 0xCA95F2B8}};
constexpr TypeText kCommercialTexts[] = {{0x3110, 0x8A95F2BF}, {0x3120, 0xAA95F2DC}, {0x3130, 0x8A95F2E4},
                                         {0x3320, 0x0A95F39A}, {0x3330, 0x6A95F3B3}};
constexpr TypeText kIndustrialTexts[] = {{0x4100, 0x4A95F2EB}, {0x4200, 0x2A95F324}, {0x4300, 0x6A95F313},
                                         {0x4400, 0xCA95F31C}};

template <std::size_t N>
void SetTypeText(void* lot, void* text, const TypeText (&table)[N])
{
    if (!lot)
        return;
    const uint32_t type = DeveloperTypeOf(lot);
    for (const TypeText& entry : table) {
        if (entry.type == type) {
            SetText(text, entry.instance);
            return;
        }
    }
}

// The share left (0x004C3B10) of what the city's port developer at `slot`
// (77 the airport's, 76 the seaport's) says of the lot (its slot 12: the
// lot, used and capacity out) as the meter's share; nothing when it says
// no. The game's two numbers are not set before the call (0 here).
void PortShare(void* lot, std::size_t slot, float* percent)
{
    void* const owner = Virtual<void*>(Global<void*>(addr::SC4_City), slot);
    uint32_t used = 0;
    uint32_t capacity = 0;
    if (Virtual<uint8_t>(owner, 0x30, lot, &used, &capacity))
        *percent = static_cast<float>(Cdecl<double>(addr::SC4QueryTool_Share, used, capacity));
}

// A record of the residential simulator (0x00B43DA4) for a school (slot
// 26) or a hospital (slot 27), made by 0x00414210 (the string empty, +0x28,
// +0x2C and +0x34 cleared; the rest 0 here where the game has what was on
// the stack); its float at +0x28 is the meter's share when the simulator
// says yes. The record's string is destroyed (0x00414250).
void RecordShare(void* occupant, std::size_t slot, float* percent)
{
    uint8_t record[tool_query::school_record::kSize] = {};
    This<void*>(addr::cSC4Advisors_F414210, static_cast<void*>(record));
    if (Virtual<uint8_t>(Global<void*>(addr::SC4_Service43DA4), slot, occupant, static_cast<void*>(record)))
        *percent = At<float>(record, tool_query::school_record::kValue);
    This<void>(addr::SC4String_Destroy, static_cast<void*>(record));
}

// A float a service gives through a place of the caller's (the place, the
// occupant; the answer points at the float): its four bytes.
uint32_t FloatOf(std::uintptr_t service, std::size_t slot, void* occupant)
{
    uint32_t place = 0;
    return *Virtual<uint32_t*>(Global<void*>(service), slot, &place, occupant);
}

// A share held to 1 as the game does it: the quotient stored as a float,
// then 1 when the (unrounded) quotient is above 1.
void StoreShare(float* percent, double share)
{
    *percent = static_cast<float>(share);
    if (share > static_cast<double>(Global<float>(addr::SC4_FloatOneA81228)))
        *percent = 1.0f;
}

// The game's 0x004D5E30 (thiscall (title, text, background picture out,
// meter picture out, meter share out), ret 0x14; caller DoMessage at
// 0x004D7464; sc4-query-ui-hooks calls it by address as
// RealGetBuildingOccupantTipInfo): the hover tip of the building at +0x2C.
// With the debug keys down (0x004C4700): no background picture, the
// exemplar's name (property 0x20) as the title, the building's debug text
// (0x004CCF60) and for a lot (the lot manager's slot 14) its developer
// shares (0x004CF2B0), demand (0x004CD820) and cell text (0x004CE8A0 with
// the zone at the centre's cell). Otherwise the
// building's name (its slot 19) as the title, then by the first of its
// purposes (property 0xAA1DD396, a uint32 array) in the order 0x1000,
// 0x1001, 0x1002, 0x1003, 0x1004, 0x1501, 0x1500, 0x1502, 0x1503, 0x1504,
// 0x1505, 0x1506, 0x1507, 0x150A, 0x150B, 0x1006 the title again
// (Assign), a background picture, for some a meter picture and share and
// for R, C and I lots the developer type's text; then the lot's level
// texts (0x004D5C90). The outs the path does not set are left as they
// come.
void __fastcall GetBuildingOccupantTipInfo(char* self, void*, void* title, void* text, uint32_t* background,
                                           uint32_t* meter, float* percent)
{
    void* const holder = Virtual<void*>(At<void*>(self, field::kOccupant), 0x18);
    void* const lot = Virtual<void*>(Global<void*>(addr::SC4_LotManager), 0x38, At<void*>(self, field::kOccupant));
    GameString name;
    MakeEmpty(name);

    if (This<uint8_t>(addr::SC4QueryTool_DebugKeysDown, self)) {
        *background = 0;
        Cdecl<uint8_t>(addr::SC4_GetPropertyString, holder, kNameProperty, title);
        Cdecl<void>(addr::SC4QueryTool_AppendBuildingText, At<void*>(self, field::kOccupant), text);
        if (lot) {
            float center[3] = {};
            Virtual<void*>(lot, 0x3C, static_cast<float*>(center));
            int32_t x = 0, z = 0;
            Virtual<void>(Global<void*>(addr::SC4_City), 0x17C, center[0], center[2], &x, &z);
            Cdecl<void>(addr::SC4QueryTool_AppendLotShares, x, z, text);
            Cdecl<void>(addr::SC4QueryTool_AppendDemandText, x, z, text);
            const uint32_t zone = Virtual<uint32_t>(Global<void*>(addr::SC4_Service43D14), 0x30, x, z);
            Cdecl<void>(addr::SC4QueryTool_AppendCellText, zone, static_cast<float*>(center), text);
        }
        Destroy(name);
        return;
    }

    if (void* const occupant = At<void*>(self, field::kOccupant)) {
        void* building = nullptr;
        if (Virtual<uint8_t>(occupant, 0x0, kBuildingIid, &building)) {
            Virtual<uint8_t>(building, 0x4C, &name);
            Release(building);
        }
    }
    char* const titleBody = BodyOf(title);
    if (reinterpret_cast<char*>(&name.begin) != titleBody) {
        uint8_t tag = 0;
        This<void>(addr::SC4String_AssignRange, titleBody, name.begin, name.end, static_cast<void*>(&tag));
    }

    void* value = nullptr;
    void* const property = Virtual<void*>(holder, 0x24, kPurposesProperty);
    if (property && Virtual<uint8_t>(property, 0x0, kValueIid, &value)) {
        if (Virtual<uint32_t>(value, 0xC) == kUint32Array) {
            const uint32_t count = Virtual<uint32_t>(value, 0x10);
            const uint32_t* const purposes = Virtual<const uint32_t*>(value, 0x19C);
            const auto has = [&](uint32_t purpose) {
                for (uint32_t i = 0; i < count; ++i) {
                    if (purposes[i] == purpose)
                        return true;
                }
                return false;
            };
            const auto setTitle = [&] { This<void>(addr::cRZBaseString_Assign, title, &name); };
            void* const occupant = At<void*>(self, field::kOccupant);
            if (has(0x1000)) {
                setTitle();
                *background = 0x14416208;
                SetTypeText(lot, text, kResidentialTexts);
            } else if (has(0x1001)) {
                setTitle();
                *background = 0x14416202;
                SetTypeText(lot, text, kCommercialTexts);
            } else if (has(0x1002)) {
                setTitle();
                *background = 0x14416206;
                SetTypeText(lot, text, kIndustrialTexts);
            } else if (has(0x1003)) {
                if (has(0x1508)) {
                    *meter = 0x14416214;
                    if (lot)
                        PortShare(lot, 0x134, percent);
                } else if (has(0x1509)) {
                    *meter = 0x14416215;
                    if (lot)
                        PortShare(lot, 0x130, percent);
                } else {
                    // The transit switch at the occupant's cell (the traffic
                    // simulator's slot 35: five dwords, the third 0 before).
                    float position[3] = {};
                    Virtual<void>(occupant, 0x20, static_cast<float*>(position));
                    uint32_t record[tool_query::transit_record::kSize / 4] = {};
                    int32_t x = 0, z = 0;
                    Virtual<void>(Global<void*>(addr::SC4_City), 0x17C, position[0], position[2], &x, &z);
                    *percent = 0.0f;
                    if (Virtual<uint8_t>(Global<void*>(addr::SC4_Service43DA8), 0x8C, x, z,
                                         static_cast<uint32_t*>(record)) &&
                        record[0] != 0) {
                        if (record[3] != 0)
                            StoreShare(percent, static_cast<double>(record[0]) / static_cast<double>(record[3]));
                        else
                            *percent = 1.0f;
                    }
                    if (has(0x1307))
                        *meter = 0x1441621C;
                    else if (has(0x1300))
                        *meter = 0x1441621B;
                    else if (has(0x1301))
                        *meter = 0x1441621A;
                    else if (has(0x1302))
                        *meter = 0x1441621C;
                    else if (has(0x1303))
                        *meter = 0x1441621C;
                    else if (has(0x1304))
                        *meter = 0x14416215;
                    else if (has(0x130B))
                        *meter = 0x1441620F;
                }
                setTitle();
                *background = 0x14416209;
            } else if (has(0x1004)) {
                setTitle();
                *background = 0x1441620A;
                if (has(0x1400)) {
                    At<uint32_t>(percent, 0) = FloatOf(addr::SC4_Service43DA0, 0xB4, occupant);
                    *meter = 0x14416218;
                } else if (has(0x1401)) {
                    // The plumbing simulator's slot 24: the occupant, 0, 0,
                    // the float out, a flag out, 0.
                    *meter = 0x14416219;
                    float water = 0.0f;
                    uint8_t flag = 0;
                    Virtual<uint8_t>(Global<void*>(addr::SC4_Service43D94), 0x60, occupant, uint32_t{0}, uint32_t{0},
                                     &water, &flag, uint32_t{0});
                    *percent = water;
                    if (flag)
                        SetText(text, 0x4A5FB66A);
                } else if (has(0x1402)) {
                    // The pollution simulator's slots 54 and 55 (both 0
                    // before): the second over the first when the second is
                    // above the float at 0x00B21854 (0x004C3730).
                    *meter = 0x14416217;
                    float first = 0.0f;
                    float second = 0.0f;
                    Virtual<void>(Global<void*>(addr::SC4_PollutionSimulator), 0xD8, occupant, &first);
                    Virtual<void>(Global<void*>(addr::SC4_PollutionSimulator), 0xDC, occupant, &second);
                    *percent = 0.0f;
                    if (This<uint8_t>(addr::SC4_FloatAbove, &second,
                                      reinterpret_cast<const float*>(addr::SC4QueryTool_FloatB21854)))
                        StoreShare(percent, static_cast<double>(second) / static_cast<double>(first));
                }
            } else if (has(0x1501)) {
                // The police simulator's float (slot 36, through a place)
                // times its number for the occupant (slot 28, unsigned),
                // truncated, is what is used of that number; the share left.
                setTitle();
                *background = 0x14416207;
                *meter = 0x14416212;
                uint32_t place = 0;
                const float factor = *Virtual<float*>(Global<void*>(addr::SC4_PoliceSimulator), 0x90, &place);
                const uint32_t number = Virtual<uint32_t>(Global<void*>(addr::SC4_PoliceSimulator), 0x70, occupant);
                const uint32_t used =
                    static_cast<uint32_t>(Ftol(static_cast<double>(number) * static_cast<double>(factor)));
                *percent = static_cast<float>(Cdecl<double>(addr::SC4QueryTool_Share, used, number));
            } else if (has(0x1500)) {
                setTitle();
                *background = 0x14416207;
                *meter = 0x14416212;
                At<uint32_t>(percent, 0) = FloatOf(addr::SC4_PoliceSimulator, 0x64, occupant);
            } else if (has(0x1502)) {
                setTitle();
                *background = 0x14416203;
                *meter = 0x14416211;
                At<uint32_t>(percent, 0) = FloatOf(addr::SC4_Service43D78, 0x28, occupant);
            } else if (has(0x1503) || has(0x1504)) {
                setTitle();
                *meter = 0x14416210;
                *background = 0x14416201;
                RecordShare(occupant, 0x68, percent);
            } else if (has(0x1505) || has(0x1506)) {
                setTitle();
                *background = 0x14416201;
                *meter = 0x14416210;
                RecordShare(occupant, 0x68, percent);
            } else if (has(0x1507)) {
                setTitle();
                *background = 0x14416201;
                *meter = 0x14416216;
                RecordShare(occupant, 0x6C, percent);
            } else if (has(0x150A) || has(0x150B)) {
                setTitle();
                *background = 0x1441620C;
            } else if (has(0x1006)) {
                setTitle();
                *background = 0x14416204;
            } else {
                setTitle();
                *background = 0x14416200;
            }
        }
        Release(value);
    }
    Cdecl<void>(addr::SC4QueryTool_AppendLotTexts, At<void*>(self, field::kOccupant), text);
    Destroy(name);
}

// --- 0x004D6C70 ------------------------------------------------------------------------------

// The texts of a cell by its zone (the jump table 0x004D7120): zones 0 and
// 15 set the title, the others the text.
constexpr uint32_t kZoneTexts[16] = {0xAA5BBDE4, 0x0A5BBDEF, 0xAA5BBE08, 0x8A5BBE12, 0x6A5BBE1F, 0x6A5BBE29,
                                     0xAA5BBE33, 0x0A5BBE3C, 0xCA5BBE4A, 0x0A5BBE51, 0x6A5BBE61, 0xAA5BBE69,
                                     0x2A5BBE6F, 0xCA5BBE78, 0x2A5BBE80, 0x2A5BBE85};
constexpr uint32_t kCellTitle = 0x6A5BBCA6;

// The game's 0x004D6C70 (thiscall (title, text), ret 8; caller DoMessage at
// 0x004D74DD): the tip of the cell under the ground point at +0x7C (x at
// +0x7C, z at +0x84; city slot 95). Nothing over water (the terrain's slot
// 34, CellHasWater). Else the title 0x6A5BBCA6 (by its body, skipped when
// the bodies are one), the zone's text (0x00B43D14 slot 12; zones above 15
// none), and for a lot at the cell (the lot manager's slot 6 with 0) the
// three texts of 0x004D4FD0 of the highest numbers appended to the text
// (the iterator stepped back by 0x006809E0), each after a line end when
// the text is not empty.
void __fastcall GetCellTipInfo(char* self, void*, void* title, void* text)
{
    int32_t x = 0, z = 0;
    void* const city = Global<void*>(addr::SC4_City);
    Virtual<void>(city, 0x17C, At<float>(self, field::kPlace), At<float>(self, field::kPlace + 8), &x, &z);
    void* const terrain = Virtual<void*>(city, 0x150);
    if (Virtual<uint8_t>(terrain, 0x88, x, z))
        return;
    const uint32_t zone = Virtual<uint32_t>(Global<void*>(addr::SC4_Service43D14), 0x30, x, z);
    uint8_t tag = 0;
    {
        LocalizedString heading;
        MakeLocalized(heading, kCellTitle);
        char* const titleBody = BodyOf(title);
        if (reinterpret_cast<char*>(&heading.text.begin) != titleBody)
            This<void>(addr::SC4String_AssignRange, titleBody, heading.text.begin, heading.text.end,
                       static_cast<void*>(&tag));
        Destroy(heading.text);
    }
    if (zone <= 15)
        SetText(zone == 0 || zone == 15 ? title : text, kZoneTexts[zone]);

    void* const lot = Virtual<void*>(Global<void*>(addr::SC4_LotManager), 0x18, x, z, uint32_t{0});
    if (!lot)
        return;
    TextMap map;
    MakeMap(map);
    Cdecl<void>(addr::SC4QueryTool_CollectLotTexts, lot, &map);
    char* const body = BodyOf(text);
    char* current = map.header;
    for (uint32_t done = 0; current != At<char*>(map.header, text_node::kLeft) && done < 3; ++done) {
        AppendNewLine(body, &tag);
        char* before = current;
        char* const node = *This<char**>(addr::SC4TreeIterator_Decrement, &before);
        AppendLine(body, node, &tag);
        This<char**>(addr::SC4TreeIterator_Decrement, &current);
    }
    FreeMap(map);
}

// --- Registration ----------------------------------------------------------------------

bool InstallQuery7b(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[5] = {};
    static const struct {
        std::uintptr_t address;
        void* ours;
    } hooks[] = {
        {addr::SC4QueryTool_InsertText, reinterpret_cast<void*>(&InsertText)},
        {addr::SC4QueryTool_CollectLotTexts, reinterpret_cast<void*>(&CollectLotTexts)},
        {addr::SC4QueryTool_AppendLotTexts, reinterpret_cast<void*>(&AppendLotTexts)},
        {addr::SC4QueryTool_GetBuildingOccupantTipInfo, reinterpret_cast<void*>(&GetBuildingOccupantTipInfo)},
        {addr::SC4QueryTool_GetCellTipInfo, reinterpret_cast<void*>(&GetCellTipInfo)},
    };
    constexpr std::size_t count = sizeof(hooks) / sizeof(hooks[0]);
    static_assert(count <= sizeof(originals) / sizeof(originals[0]));
    for (std::size_t i = 0; i < count; ++i) {
        const addr::Entry* found = addr_check::Find(hooks[i].address);
        if (!found || !hook::Install(*found, hooks[i].ours, &originals[i]))
            return false;
    }
    return true;
}

const bool g_registered = registry::Register({
    "ui.tools.query7b",
    "ui",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4QueryTool_GetBuildingOccupantTipInfo,
    "Our query tool, seventh piece, second half: the building and cell tips and a lot's level texts "
    "(docs/tools.md)",
    false,
    InstallQuery7b,
});

} // namespace
} // namespace opensc4::tool_query_7b
