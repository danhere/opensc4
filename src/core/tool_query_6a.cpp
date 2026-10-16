// The query tool of the city view, sixth piece without the cell log: the
// traffic text of a network piece, fillers, the lot's developer and the
// query dialog, 0x004D07C0 to 0x004D1225 and 0x004D3260 to 0x004D4131.
// Replacement, off by default:
//
//   ui.tools.query6a: 22 functions of the query tool
//     (cSC4ViewInputControlQuery: a view input control of 0xA0 bytes, vtable
//     0x00A90A88; docs/tools.md): the traffic of a network piece by route
//     kind (0x004D07C0), an amount of money with cents into a text control
//     (0x004D0D30), eleven fillers, the wealth a lot is built for
//     (0x004D3260), the growth developer of a lot (0x004D32D0), the text of
//     a map level at an occupant's lot (0x004D3470), the filler-table reader
//     (0x004D3A90) and three walks over a window's children (0x004D3AE0,
//     0x004D3B40, 0x004D3BD0), the query dialog of an occupant (0x004D3C30)
//     and the window procedure of the query's window runner (0x004D3F40).
//
// Entries of the filler table at 0x00A92488 that are functions of this
// file (index, address): 57 0x004D10F0 (50 too, through the five-byte jump
// at 0x004D3250, left the game's), 63 0x004D1190, 83 0x004D3500, 84
// 0x004D3730, 85 0x004D3810, 87 0x004D3880, 89 0x004D39B0, 90 0x004D3A20,
// 116 0x004D3940, 117 0x004D37A0, 121 0x004D0EE0. None changes the city:
// all of them answer without `fill` at once (121 with false, the others
// with true).
//
// Left the game's: SC4BuildingProfile::CopyConstruct 0x004D1230 (callers
// outside the class), the cell log 0x004D12A0 (ui.tools.query6b), the jump
// 0x004D3250. Nothing here draws.
//
// NAM: nam-dll (EnableDualNetworkTransitPatch) writes a JMP at 0x004D09D1
// inside 0x004D07C0 (its own route table for rail, monorail added). The
// row in foreign_sites.cpp makes the hook layer leave 0x004D07C0 the game's
// when that JMP is there; nothing of ours calls 0x004D07C0.
// sc4-query-ui-hooks calls 0x004D3C30 by address from its wrappers of the
// calls at 0x004D4494 and 0x0040CA7A: with ours at the entry the wrapper
// reaches ours.
//
// No random number is drawn. 0x004D3C30 runs the dialog modal through the
// window manager (slot 41, DoModalWin: real time, the player's clicks) and,
// while the dialog shows an occupant the view marks, a cRZTimer of its own
// (real time, 330 ms; its callback SC4QueryTool::TimerSend 0x004C4EE0 sends
// the message 0x0A456D96). The dialog's read-back walk (when the player
// answers OK) calls every filler without `fill`: renames and funding.
// 0x004D3F40 calls the slider's own function (a filler, without `fill`)
// when a slider moves.
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"
#include "core/tool_query.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace opensc4::tool_query_6a {
namespace {

namespace field = tool_query::field;
namespace window_runner = tool_query::window_runner;
namespace window_message = tool_query::window_message;
namespace profile_field = tool_query::building_profile;

constexpr uint32_t kNetworkOccupant = 0xA821EF94; // the network occupant's interface
constexpr uint32_t kTextControl = 0x212CDC1F;     // a text control (its slot 4: the caption)
constexpr uint32_t kNumberControl = 0x21325207;   // a slider or other number control
constexpr uint32_t kPictureControl = 0xC12CEA13;  // what a picture control is asked for
constexpr uint32_t kTextEdit = 0x231A1C57;        // a text edit control
constexpr uint32_t kOtherSkipped = 0x8810;        // the other interface 0x004D3B40 leaves alone
constexpr uint32_t kWindowIid = 0x22BA0121;
constexpr uint32_t kProcedureIid = 0x5386D516;    // a window's procedure part (the runner takes it)
constexpr uint32_t kLoadFlags = 0x10000005;       // LoadWindowChild's third argument
constexpr uint32_t kQueryTextGroup = 0xEA5524EB;  // the group of the query's game strings
constexpr uint32_t kAllRoutes = 0xEBB0F666;       // the check box of the tool's panel: all routes
constexpr uint32_t kNoRoutes = 0x2BAAB5C0;        // the game string for no traffic
constexpr uint32_t kLayoutProperty = 0x2A499F85;  // an occupant's query dialog (layout instance)
constexpr uint32_t kLeftProperty = 0x8ADC29D4;    // a picture's first left edge, kept on its window
constexpr uint32_t kCostProperty = 0x2AA6E8AD;    // a slider's cost (64 bits)
constexpr uint32_t kHandlerProperty = 0x6A8CC9D6; // the function a slider calls when it moves
constexpr uint32_t kIdsProperty = 0x0AB3F6DB;     // the control IDs a slider fills again
constexpr uint32_t kAbandoned = 4;                // the lot state (slot 33) of an abandoned lot
constexpr uint32_t kUnknownCause = 4;             // the vacate reason (slot 45) with its own text
constexpr uint32_t kNoReasonFifth = 0x0A71FAD3;   // the text for that reason
constexpr uint32_t kMarkerTicks = 0x14A;          // the dialog's timer, ms
constexpr float kThird = 0.333333343f;            // 0x00A92480
constexpr float kOne = 1.0f;                      // 0x00A81228
constexpr uint32_t kOneBits = 0x3F800000;
constexpr double kHundredth = 0.01;               // 0x00A92688
constexpr double kTwoTo32 = 4294967296.0;         // 0x00A80AB8

// The route kinds of 0x004D07C0's three tables (kind, game string; ended by
// kind 9 with no string): roads and the like, subways, rail.
struct RouteName {
    uint32_t kind;
    uint32_t name;
};
constexpr RouteName kRoadRoutes[] = {{0, 0x2B8B39FE}, {1, 0x2B8B3A10}, {2, 0x8B8B3A1A}, {4, 0x8B8B3AB4},
                                     {7, 0x2BC7E0D2}, {8, 0xCC05A60F}, {9, 0}};
constexpr RouteName kSubwayRoutes[] = {{6, 0xAB8B3A4A}, {9, 0}};
constexpr RouteName kRailRoutes[] = {{3, 0x4B8B4693}, {5, 0xCB8C5C08}, {9, 0}};

// The growth developer types 0x004D32D0 weighs for a zone (the game's lists
// at 0x00B09780, 0x00B09768, 0x00B09760, 0x00B09754 and 0x00B09744, each
// ended by 0).
constexpr uint32_t kResidentialTypes[] = {0x1010, 0x1020, 0x1030, 0};
constexpr uint32_t kCommercialTypes[] = {0x3110, 0x3120, 0x3130, 0x3320, 0x3330, 0};
constexpr uint32_t kAgricultureTypes[] = {0x4100, 0};
constexpr uint32_t kMediumIndustryTypes[] = {0x4400, 0x4300, 0};
constexpr uint32_t kHighIndustryTypes[] = {0x4200, 0x4400, 0x4300, 0};

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

void AddRef(void* object)
{
    Virtual<uint32_t>(object, 0x4);
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

// An unsigned dword as fild with the 2 to the 32nd added when negative.
double Unsigned(uint32_t value)
{
    double whole = static_cast<double>(static_cast<int32_t>(value));
    if (static_cast<int32_t>(value) < 0)
        whole += kTwoTo32;
    return whole;
}

uint32_t BitsOf(float value)
{
    uint32_t bits;
    static_assert(sizeof bits == sizeof value);
    std::memcpy(&bits, &value, sizeof bits);
    return bits;
}

// --- The game's strings --------------------------------------------------------------------

// The body of an STL string: begin, end, end of the buffer.
struct StringBody {
    char* begin;
    char* end;
    char* capacity;
};

// SC4String as the game keeps one on the stack: the vtable 0x00A80810, an
// STL body and a zero dword.
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

// The game string into `text`: Assign (0x00424380) from a localized string,
// which is then destroyed.
void SetText(void* text, uint32_t instance)
{
    LocalizedString made;
    MakeLocalized(made, instance);
    This<void>(addr::cRZBaseString_Assign, text, &made);
    Destroy(made.text);
}

// The same through the body's AssignRange (0x00408A70), skipped when the
// bodies are one, as the game does for some of them.
void SetTextByRange(void* text, uint32_t instance)
{
    LocalizedString made;
    MakeLocalized(made, instance);
    char* const body = BodyOf(text);
    if (reinterpret_cast<char*>(&made.text.begin) != body) {
        uint8_t tag = 0;
        This<void>(addr::SC4String_AssignRange, body, made.text.begin, made.text.end, static_cast<void*>(&tag));
    }
    Destroy(made.text);
}

// --- The traffic of a network piece ------------------------------------------------------------

// The game's 0x004D07C0 (thiscall: title, text; ret 8; caller DoMessage
// 0x004D7160; nam-dll's GetNetworkOccupantTrafficInfo): for the network
// piece at +0x2C (QueryInterface 0xA821EF94; nothing without one) the title
// by its first type of 0 road (by range), 1 rail, 2, 3, 7 subway, 6, 8, 9,
// 0xA, 0xB, 0xC (network slot 22), and for each route kind of the type's
// table (roads and the like, subway, rail) with traffic at the piece's cell
// (occupant slot 11; traffic simulator 0x00B43DA8 slot 12,
// GetTrafficEdgeDensity: cell x, cell z, kind, whether the panel's check
// box 0xEBB0F666 is checked; the cell is the first corner of
// GetBoundingCityCells, four dwords) a line "name: count" (the word at the
// answer's +0x20) appended to the text, a new line before each but the
// first. A text still empty gets the game string 0x2BAAB5C0. A type of
// none of these: no title, only that. An 8-byte buffer the game allocates
// first and frees at the end is never used (kept). The check box's flag is
// pushed as a dword whose upper bytes are the game's stack leftovers (0
// here); the cell is not set before slot 11 writes it.
void __fastcall NetworkTrafficInfo(char* self, void*, void* title, void* text)
{
    void* const occupant = At<void*>(self, field::kOccupant);
    void* network = nullptr;
    if (!Virtual<uint8_t>(occupant, 0x0, kNetworkOccupant, &network))
        return;
    void* const traffic = Global<void*>(addr::SC4_Service43DA8);
    char* const unused = Cdecl<char*>(addr::SC4_stl_allocate, uint32_t{8});
    *unused = 0;
    uint32_t checked = 0;
    if (Cdecl<uint8_t>(addr::SC4Win_IsChildChecked, At<void*>(self, field::kPanel), kAllRoutes))
        checked = 1;
    int32_t cell[4] = {0, 0, 0, 0}; // GetBoundingCityCells: min x, min z, max x, max z
    Virtual<uint8_t>(occupant, 0x2C, static_cast<int32_t*>(cell));

    const auto is = [&](uint32_t type) { return Virtual<uint8_t>(network, 0x58, type) != 0; };
    const RouteName* routes = kRoadRoutes;
    if (is(0)) {
        SetTextByRange(title, 0x6A5589D4);
    } else if (is(1)) {
        SetText(title, 0x4A5589F3);
        routes = kRailRoutes;
    } else if (is(2)) {
        SetText(title, 0xAA5589FB);
    } else if (is(3)) {
        SetText(title, 0x8A558A04);
    } else if (is(7)) {
        SetText(title, 0x8A558A10);
        routes = kSubwayRoutes;
    } else if (is(6)) {
        SetText(title, 0x8B8C5CD0);
    } else if (is(8)) {
        SetText(title, 0x8B8C5CBD);
    } else if (is(9)) {
        SetText(title, 0x0BE09CA2);
    } else if (is(0xA)) {
        SetText(title, 0x4BD9FDD8);
    } else if (is(0xB)) {
        SetText(title, 0xCBE09C9B);
    } else if (is(0xC)) {
        SetText(title, 0x2BE09C9E);
    } else {
        routes = nullptr;
    }

    char* const body = BodyOf(text);
    for (const RouteName* route = routes; route && route->name; ++route) {
        char* const density = Virtual<char*>(traffic, 0x30, static_cast<uint32_t>(cell[0]),
                                             static_cast<uint32_t>(cell[1]), route->kind, checked);
        const uint16_t count = At<uint16_t>(density, 0x20);
        if (count == 0)
            continue;
        GameString line;
        MakeEmpty(line);
        LocalizedString name;
        MakeLocalized(name, route->name);
        GameString copy;
        uint8_t tag = 0;
        This<void>(addr::SC4String_CopyBuffer, static_cast<void*>(&copy.begin), static_cast<void*>(&name.text.begin),
                   uint32_t{0}, uint32_t{0xFFFFFFFF}, static_cast<void*>(&tag));
        copy.vtable = addr::SC4String_Vtable;
        copy.zero = 0;
        Destroy(name.text);
        Cdecl<void>(addr::cRZBaseString_Sprintf, &line, "%s: %d", This<const char*>(addr::cRZBaseString_Data, &copy),
                    static_cast<int32_t>(count));
        if (!BodyEmpty(body))
            This<void>(addr::SC4String_PushBack, body, uint32_t{'\n'});
        This<void>(addr::SC4String_AppendRange, body, line.begin, line.end, static_cast<void*>(&tag));
        Destroy(copy);
        Destroy(line);
    }

    if (BodyEmpty(body))
        SetTextByRange(text, kNoRoutes);
    Release(network);
    Cdecl<void>(addr::SC4_Free, static_cast<void*>(unused));
}

// --- Money with cents into a text control --------------------------------------------------------

// The game's 0x004D0D30 (cdecl: window, control ID, a double; callers the
// fillers 57 and 63): the amount formatted by the application's number
// formatter (the app's slot 38 with 0, its slot 19: value, out, format
// "%.2f") with the money sign before it (a function-local static string
// "\xC2\xA7", the game's own: 0x00B21904, guard 0x00B21918 bit 1, exit
// handler 0x00A7B170 given to atexit 0x009EF059; joined by 0x00408AE0 and
// copied back through 0x00408420 and AssignRange, as the game does) into
// the text control (0x212CDC1F, slot 4). False without the window or the
// child. The strings' allocator tag is the address of the window argument,
// as in the game.
uint8_t __cdecl SetChildMoneyFloat(void* window, uint32_t id, double value)
{
    GameString text;
    MakeEmpty(text);
    void* const formatter = Virtual<void*>(Cdecl<void*>(addr::SC4_CurrentApp), 0x98, uint32_t{0});
    static const char kFormat[] = "%.2f";
    GameString format;
    format.begin = nullptr;
    format.end = nullptr;
    format.capacity = nullptr;
    This<void>(addr::SC4String_FromRange, static_cast<void*>(&format.begin), &kFormat[0], &kFormat[4],
               static_cast<void*>(&window));
    format.vtable = addr::SC4String_Vtable;
    format.zero = 0;
    Virtual<void>(formatter, 0x4C, value, &text, &format);
    Destroy(format);

    uint32_t& guard = Global<uint32_t>(addr::SC4QueryTool_MoneySignGuard);
    if (!(static_cast<uint8_t>(guard) & 1)) {
        guard |= 1;
        This<void*>(addr::SC4String_FromText, reinterpret_cast<void*>(addr::SC4QueryTool_MoneySign), "\xC2\xA7");
        Cdecl<int32_t>(addr::SC4_atexit, addr::SC4QueryTool_MoneySignExit);
    }
    StringBody joined;
    StringBody* const both = Cdecl<StringBody*>(addr::cSC4Advice_F408AE0, &joined,
                                                reinterpret_cast<void*>(addr::SC4QueryTool_MoneySign + 4),
                                                static_cast<void*>(&text.begin));
    char* const begin = both->begin;
    char* const end = both->end;
    StringBody made = {nullptr, nullptr, nullptr};
    This<void>(addr::cSC4Advice_F408420, &made, begin, end, static_cast<void*>(&window));
    This<void>(addr::SC4String_AssignRange, static_cast<void*>(&text.begin), made.begin, made.end,
               static_cast<void*>(&window));
    if (made.begin)
        Cdecl<void>(addr::SC4_Free, static_cast<void*>(made.begin));
    if (joined.begin)
        Cdecl<void>(addr::SC4_Free, static_cast<void*>(joined.begin));

    uint8_t done = 0;
    void* control = nullptr;
    if (window && Virtual<uint8_t>(window, 0x94, id, kTextControl, &control)) {
        Virtual<void>(control, 0x10, &text);
        Release(control);
        done = 1;
    }
    Destroy(text);
    return done;
}

// --- Fillers -------------------------------------------------------------------------------

// The game's 0x004D0EE0 (table index 121): with `fill`, for an abandoned
// lot (lot manager slot 14; lot slot 33 is 4) the text of its vacate reason
// (slot 45: 1 to 4 and 6; 5 and others none), the control shown when there
// is a text (SC4Win::ShowChild, the flag in a dword whose upper bytes are
// the game's leftovers: 0 here) and given it. True; false without `fill`.
uint8_t __cdecl FillVacateReason(void* window, uint32_t id, uint32_t fill, void* occupant)
{
    if (!static_cast<uint8_t>(fill))
        return 0;
    GameString text;
    MakeEmpty(text);
    void* const lot = Virtual<void*>(Global<void*>(addr::SC4_LotManager), 0x38, occupant);
    if (lot && Virtual<uint32_t>(lot, 0x84) == kAbandoned) {
        switch (Virtual<uint32_t>(lot, 0xB4)) {
        case 1:
            SetText(&text, 0xCC06A5DE);
            break;
        case 2:
            SetText(&text, 0xCC06A5DF);
            break;
        case 3:
            SetText(&text, 0xCC06A5E0);
            break;
        case 4:
            SetText(&text, 0xCC06A5E1);
            break;
        case 6:
            SetText(&text, 0xCC06A5E2);
            break;
        default:
            break;
        }
    }
    Cdecl<void>(addr::SC4Win_ShowChild, window, id, uint32_t{text.begin != text.end}, uint32_t{0});
    void* control = nullptr;
    if (window && Virtual<uint8_t>(window, 0x94, id, kTextControl, &control)) {
        Virtual<void>(control, 0x10, &text);
        Release(control);
    }
    Destroy(text);
    return 1;
}

// The game's 0x004D10F0 (table index 57, and 50 through the jump at
// 0x004D3250; also called by the cell log 0x004D12A0): with `fill`, a power
// plant's monthly cost (power simulator slot 49) over its first figure of
// slot 33 (GetOccupantPowerProduced: occupant, first, second), 0 when that
// is 0, as money with cents (0x004D0D30). The cost is not set before slot
// 49 and read even when it says no (0 here). True without `fill`.
uint8_t __cdecl FillPowerUnitCost(void* window, uint32_t id, uint32_t fill, void* occupant)
{
    if (!static_cast<uint8_t>(fill))
        return 1;
    void* const power = Global<void*>(addr::SC4_Service43DA0);
    uint32_t first = 0;
    uint32_t second = 0;
    Virtual<void>(power, 0x84, occupant, &first, &second);
    double value;
    if (first) {
        int64_t monthly = 0;
        Virtual<uint8_t>(power, 0xC4, occupant, &monthly);
        value = static_cast<double>(monthly) / Unsigned(first);
    } else {
        value = 0.0;
    }
    return SetChildMoneyFloat(window, id, value);
}

// The game's 0x004D1190 (table index 63): with `fill`, a water producer's
// monthly upkeep (plumbing simulator slot 26) over its production (slot
// 24, GetWaterProducerInfo, the other places null), 0 when that is 0, as
// money with cents (0x004D0D30). The production's place is the `fill`
// argument's (not cleared: what slot 24 does not write stays `fill`).
// False when either slot says no; true without `fill`.
uint8_t __cdecl FillWaterUnitCost(void* window, uint32_t id, uint32_t fill, void* occupant)
{
    if (!static_cast<uint8_t>(fill))
        return 1;
    void* const water = Global<void*>(addr::SC4_Service43D94);
    uint32_t production = fill;
    if (!Virtual<uint8_t>(water, 0x60, occupant, &production, static_cast<uint32_t*>(nullptr),
                          static_cast<uint32_t*>(nullptr), static_cast<uint8_t*>(nullptr),
                          static_cast<uint32_t*>(nullptr)))
        return 0;
    int64_t upkeep = 0;
    if (!Virtual<uint8_t>(water, 0x68, occupant, &upkeep))
        return 0;
    const double value = production ? static_cast<double>(upkeep) / Unsigned(production) : 0.0;
    return SetChildMoneyFloat(window, id, value);
}

// --- A lot's wealth and developer --------------------------------------------------------------

// The game's 0x004D3260 (cdecl: lot; callers 0x004D32D0, 0x004D3500,
// 0x004D4FD0, 0x004D5E30): with a building type (lot slot 46 with 0) the
// wealth of its building profile (development service slot 39, copied whole
// by 0x004D1230 and the copy's vector destroyed, as the game does); without
// one the occupant wealth (lot slot 41). The vector's allocator tag is the
// address of the lot argument, as in the game.
uint32_t __cdecl LotWealth(void* lot)
{
    const uint32_t type = Virtual<uint32_t>(lot, 0xB8, uint32_t{0});
    if (!type)
        return Virtual<uint32_t>(lot, 0xA4);
    void* const profile = Virtual<void*>(Global<void*>(addr::SC4_Service43D68), 0x9C, type);
    alignas(4) char copy[profile_field::kSize];
    This<void*>(addr::SC4BuildingProfile_CopyConstruct, static_cast<void*>(copy), profile);
    char* const begin = At<char*>(copy, profile_field::kVectorBegin);
    char* const end = At<char*>(copy, profile_field::kVectorEnd);
    Cdecl<void>(addr::SC4MapVector_DestroyRange, begin, end, static_cast<void*>(&lot));
    if (begin)
        Cdecl<void>(addr::SC4_Free, static_cast<void*>(begin));
    return At<uint32_t>(copy, profile_field::kWealth);
}

// A lot's cell: its centre (slot 15) to a cell (city slot 95, x and z of
// the centre).
void CellOfLot(void* lot, int32_t& x, int32_t& z)
{
    float centre[3];
    Virtual<void>(lot, 0x3C, static_cast<float*>(centre));
    Virtual<uint8_t>(Global<void*>(addr::SC4_City), 0x17C, centre[0], centre[2], &x, &z);
}

// The game's 0x004D32D0 (cdecl: lot; callers 0x004D3470, 0x004D4FD0): the
// growth developer the map levels are read from. With a building type (lot
// slot 46 with 0): the development service's developer (slot 32) of the
// type (slot 30) for the profile's purpose (slot 39's +0) and the lot's
// wealth (0x004D3260); null when the purpose is 0. Without one: of the
// developer types of the zone at the lot's cell (zone manager slot 12;
// zones 1 to 9), the developer (slot 32) with the highest value at the cell
// (its slot 29, 16 bits signed), on a tie the highest truncated value of its
// slot 18's object's slot 12; both start at -1000; null when none is higher
// or for another zone.
void* __cdecl DeveloperOfLot(void* lot)
{
    int32_t cellX = 0;
    int32_t cellZ = 0;
    CellOfLot(lot, cellX, cellZ);
    const uint32_t zone = Virtual<uint32_t>(Global<void*>(addr::SC4_Service43D14), 0x30, cellX, cellZ);
    void* const development = Global<void*>(addr::SC4_Service43D68);
    const uint32_t type = Virtual<uint32_t>(lot, 0xB8, uint32_t{0});
    if (type) {
        const uint32_t* const profile = Virtual<const uint32_t*>(development, 0x9C, type);
        const uint32_t wealth = LotWealth(lot);
        const uint32_t purpose = profile[profile_field::kPurpose / 4];
        if (!purpose)
            return nullptr;
        const uint32_t developerType = Virtual<uint32_t>(development, 0x78, purpose, wealth);
        return Virtual<void*>(development, 0x80, developerType);
    }
    const uint32_t* types;
    switch (zone) {
    case 1:
    case 2:
    case 3:
        types = kResidentialTypes;
        break;
    case 4:
    case 5:
    case 6:
        types = kCommercialTypes;
        break;
    case 7:
        types = kAgricultureTypes;
        break;
    case 8:
        types = kMediumIndustryTypes;
        break;
    case 9:
        types = kHighIndustryTypes;
        break;
    default:
        return nullptr;
    }
    int32_t best = -1000;
    int32_t bestSecond = -1000;
    void* chosen = nullptr;
    for (; *types; ++types) {
        void* const developer = Virtual<void*>(development, 0x80, *types);
        const int32_t level = static_cast<int16_t>(Virtual<uint16_t>(developer, 0x74, cellX, cellZ));
        void* const holder = Virtual<void*>(developer, 0x48);
        const int32_t second = Ftol(Virtual<double>(holder, 0x30));
        if (level > best || (level == best && second > bestSecond)) {
            best = level;
            bestSecond = second;
            chosen = developer;
        }
    }
    return chosen;
}

// The game's 0x004D3470 (cdecl: occupant, a level function, a table of
// game strings, a string; callers the seven level fillers): for the
// occupant's lot (lot manager slot 14) and its developer (0x004D32D0) the
// level function (0x004C4750 and the like: developer, cell x, cell z, an
// out the game neither sets nor reads) gives the index into the table; that
// game string is loaded into the string (0x00603040). Nothing without a lot
// or a developer.
void __cdecl OccupantLevelText(void* occupant, std::uintptr_t level, const uint32_t* names, void* text)
{
    void* const lot = Virtual<void*>(Global<void*>(addr::SC4_LotManager), 0x38, occupant);
    if (!lot)
        return;
    int32_t cellX = 0;
    int32_t cellZ = 0;
    CellOfLot(lot, cellX, cellZ);
    void* const developer = DeveloperOfLot(lot);
    if (!developer)
        return;
    int32_t unused = 0;
    const int32_t index = Cdecl<int32_t>(level, developer, cellX, cellZ, &unused);
    Cdecl<void>(addr::SC4_LoadLocalizedText, text, kQueryTextGroup, names[index], uint32_t{0});
}

// --- More fillers ----------------------------------------------------------------------------

// The game's 0x004D3500 (table index 83): with `fill` and an occupant on a
// lot whose wealth (0x004D3260) is not 0: the picture control's window
// (0xC12CEA13, slot 3) gets its left edge moved so that the bar is a third
// of the wealth wide (at most whole): its rectangle (slot 48) with the left
// edge it had the first time (kept as the property 0x8ADC29D4; a script
// value made and kept when there is none) plus the width less the share,
// truncated (slot 54). The control's value (0x004C3CA0) is the occupant
// wealth (lot slot 41) over that wealth, at most 1. Without an occupant,
// a lot or a wealth the value is 1. The answer is 0x004C3CA0's; true
// without `fill`. A failed allocation calls through null, as in the game.
uint8_t __cdecl FillWealthBar(void* window, uint32_t id, uint32_t fill, void* occupant)
{
    if (!static_cast<uint8_t>(fill))
        return 1;
    if (!occupant)
        return Cdecl<uint8_t>(addr::SC4QueryTool_SetChildValue, window, id, kOneBits);
    void* const lot = Virtual<void*>(Global<void*>(addr::SC4_LotManager), 0x38, occupant);
    if (!lot)
        return Cdecl<uint8_t>(addr::SC4QueryTool_SetChildValue, window, id, kOneBits);
    const uint32_t wealth = LotWealth(lot);
    const uint32_t occupantWealth = Virtual<uint32_t>(lot, 0xA4);
    if (!wealth)
        return Cdecl<uint8_t>(addr::SC4QueryTool_SetChildValue, window, id, kOneBits);
    const double whole = Unsigned(wealth);
    const double scaled = static_cast<double>(kThird) * whole;
    float bar = static_cast<float>(scaled);
    if (scaled > static_cast<double>(kOne))
        bar = kOne;
    const double ratio = Unsigned(occupantWealth) / whole;
    float share = static_cast<float>(ratio);
    if (ratio > static_cast<double>(kOne))
        share = kOne;

    void* picture = nullptr;
    if (Virtual<uint8_t>(window, 0x94, id, kPictureControl, &picture)) {
        const int32_t* const rect = Virtual<const int32_t*>(Virtual<void*>(picture, 0xC), 0xC0);
        int32_t box[4] = {rect[0], rect[1], rect[2], rect[3]};
        void* left = nullptr;
        if (Virtual<uint8_t>(Virtual<void*>(picture, 0xC), 0x1C4, kLeftProperty, &left)) {
            box[0] = *Virtual<const int32_t*>(left, 0x124);
            Release(left);
        } else {
            void* const memory = Cdecl<void*>(addr::SC4_operator_new, uint32_t{0x14});
            left = memory ? This<void*>(addr::SC4ScriptNumber_Construct, memory) : nullptr;
            AddRef(left);
            Virtual<void>(left, 0x68, box[0]);
            Virtual<void>(Virtual<void*>(picture, 0xC), 0x1C8, kLeftProperty, left);
        }
        const double width = static_cast<double>(box[2] - box[0]);
        box[0] += Ftol(width - static_cast<double>(bar) * width);
        Virtual<void>(Virtual<void*>(picture, 0xC), 0xD8, static_cast<int32_t*>(box));
    }
    const uint8_t result = Cdecl<uint8_t>(addr::SC4QueryTool_SetChildValue, window, id, BitsOf(share));
    if (picture)
        Release(picture);
    return result;
}

// What the level fillers share: with `fill` and an occupant, the text of the
// level (0x004D3470 with the level function and the game's table at
// 0x00A90960) in a localized string made empty (0x00603000) to the text
// control (0x004C3C00). True.
uint8_t LevelFiller(void* window, uint32_t id, uint32_t fill, void* occupant, std::uintptr_t level)
{
    if (!static_cast<uint8_t>(fill) || !occupant)
        return 1;
    LocalizedString text;
    This<void*>(addr::SC4LocalizedString_ConstructBase, &text);
    OccupantLevelText(occupant, level, reinterpret_cast<const uint32_t*>(addr::SC4QueryTool_LevelNames), &text);
    Cdecl<uint8_t>(addr::SC4QueryTool_SetChildText, window, id, static_cast<void*>(&text));
    Destroy(text.text);
    return 1;
}

// The game's 0x004D3730 (table index 84): the pollution level (0x004C5C90).
uint8_t __cdecl FillLevelPollution(void* window, uint32_t id, uint32_t fill, void* occupant)
{
    return LevelFiller(window, id, fill, occupant, addr::SC4QueryTool_LevelPollution);
}

// The game's 0x004D37A0 (table index 117): the garbage level (0x004C5D70).
uint8_t __cdecl FillLevelGarbage(void* window, uint32_t id, uint32_t fill, void* occupant)
{
    return LevelFiller(window, id, fill, occupant, addr::SC4QueryTool_LevelGarbage);
}

// The game's 0x004D3810 (table index 85): the third level (0x004C4890).
uint8_t __cdecl FillLevelThird(void* window, uint32_t id, uint32_t fill, void* occupant)
{
    return LevelFiller(window, id, fill, occupant, addr::SC4QueryTool_LevelThird);
}

// The game's 0x004D3940 (table index 116): the fourth level (0x004C4930).
uint8_t __cdecl FillLevelFourth(void* window, uint32_t id, uint32_t fill, void* occupant)
{
    return LevelFiller(window, id, fill, occupant, addr::SC4QueryTool_LevelFourth);
}

// The game's 0x004D39B0 (table index 89): the first level (0x004C4750).
uint8_t __cdecl FillLevelFirst(void* window, uint32_t id, uint32_t fill, void* occupant)
{
    return LevelFiller(window, id, fill, occupant, addr::SC4QueryTool_LevelFirst);
}

// The game's 0x004D3A20 (table index 90): the second level (0x004C47F0).
uint8_t __cdecl FillLevelSecond(void* window, uint32_t id, uint32_t fill, void* occupant)
{
    return LevelFiller(window, id, fill, occupant, addr::SC4QueryTool_LevelSecond);
}

// The game's 0x004D3880 (table index 87): with `fill` and an occupant, for
// an abandoned lot whose vacate reason is 4 the game string 0x0A71FAD3;
// otherwise the fifth level (0x004C4A30) with the game's table at
// 0x00A9096C. To the text control (0x004C3C00). True.
uint8_t __cdecl FillLevelFifth(void* window, uint32_t id, uint32_t fill, void* occupant)
{
    if (!static_cast<uint8_t>(fill) || !occupant)
        return 1;
    LocalizedString text;
    This<void*>(addr::SC4LocalizedString_ConstructBase, &text);
    void* const lot = Virtual<void*>(Global<void*>(addr::SC4_LotManager), 0x38, occupant);
    if (lot && Virtual<uint32_t>(lot, 0x84) == kAbandoned && Virtual<uint32_t>(lot, 0xB4) == kUnknownCause)
        Cdecl<void>(addr::SC4_LoadLocalizedText, static_cast<void*>(&text), kQueryTextGroup, kNoReasonFifth,
                    uint32_t{0});
    else
        OccupantLevelText(occupant, addr::SC4QueryTool_LevelFifth,
                          reinterpret_cast<const uint32_t*>(addr::SC4QueryTool_LevelNamesFifth), &text);
    Cdecl<uint8_t>(addr::SC4QueryTool_SetChildText, window, id, static_cast<void*>(&text));
    Destroy(text.text);
    return 1;
}

// --- The filler table and the walks ------------------------------------------------------------

// The game's 0x004D3A90 (cdecl: window, control ID, fill, what is asked
// about): the filler of the table at 0x00A92488 for the ID's low 12 bits,
// when the ID has the bit 0x1000, the window and `about` are set, the index
// is below 0x7F and the entry is not null; its answer, false otherwise.
uint8_t __cdecl FillControl(void* window, uint32_t id, uint32_t fill, void* about)
{
    if (!(id & 0x1000))
        return 0;
    const uint32_t index = id & 0xFFF;
    if (!window || !about || index >= tool_query::kFillerCount)
        return 0;
    const std::uintptr_t filler = Global<std::uintptr_t>(addr::SC4QueryTool_Fillers + 4 * index);
    if (!filler)
        return 0;
    return Cdecl<uint8_t>(filler, window, id, fill, about);
}

// What the walks 0x004D3AE0 and 0x004D3BD0 share (cdecl; what a window's
// slot 32 calls for each child: the child, its ID, the child as the
// interface 0x22BA0121, the walk's value, here the occupant): the filler
// for the child (as 0x004D3A90, in line in the game); when that says no,
// the same walk over the child's children, given the game's address of the
// walk as the game gives it. True.
uint8_t Walk(void* child, uint32_t id, void* window, void* about, uint32_t fill, std::uintptr_t self)
{
    if (!FillControl(child, id, fill, about))
        Virtual<void>(window, 0x80, kWindowIid, self, about);
    return 1;
}

// The game's 0x004D3AE0: the walk with fill (0x004D3C30 fills its dialog
// with it).
uint8_t __cdecl FillChildren(void* child, uint32_t id, void* window, void* about)
{
    return Walk(child, id, window, about, 1, addr::SC4QueryTool_FillChildren);
}

// The game's 0x004D3BD0: the walk without fill (0x004D3C30 reads its
// dialog back with it when the answer is OK).
uint8_t __cdecl ReadChildren(void* child, uint32_t id, void* window, void* about)
{
    return Walk(child, id, window, about, 0, addr::SC4QueryTool_ReadChildren);
}

// The game's 0x004D3B40: the walk with fill that leaves text edits
// (0x231A1C57) and children of the interface 0x8810 alone (true at once);
// 0x004D3C30 keeps it at 0x00B21848 for DoMessage. The text edit's
// QueryInterface writes into the window argument's place, as in the game
// (a failed one leaves it). Others as 0x004D3AE0, through 0x004D3A90.
uint8_t __cdecl FillChildrenNotEdits(void* child, uint32_t id, void* window, void* about)
{
    if (Virtual<uint8_t>(child, 0x0, kTextEdit, &window)) {
        Release(window);
        return 1;
    }
    void* other = nullptr;
    if (Virtual<uint8_t>(child, 0x0, kOtherSkipped, &other)) {
        Release(other);
        return 1;
    }
    if (!FillControl(child, id, 1, about))
        Virtual<void>(window, 0x80, kWindowIid, addr::SC4QueryTool_FillChildrenNotEdits, about);
    return 1;
}

// --- The query dialog ------------------------------------------------------------------------

// The game's 0x004D3C30 (cdecl: occupant; callers 0x0040C9B0 and 0x004D4140;
// sc4-query-ui-hooks' DoQueryDialog): the occupant's dialog. Its layout
// instance is the property 0x2A499F85 (false without one); the layout {0,
// 0x96A006B0, instance} loaded (0x005F9390; false without it). The view's
// markers (city view 0x00B43DC4, slot 49): the kind at +0x14 kept; the
// occupant's (0x007D1AB0) set and shown (0x007D1D10, 0x007D20E0) when it
// has one, with a timer of the function's own started (callback
// 0x004C4EE0, 330 ms). The window (slot 23) gets a runner (0x004C5620 for
// its procedure part, kept in a reference holder; the runner's slot 6 the
// occupant; the procedure's slot 7 the runner's procedure part) and the
// bar painter (4 bytes, vtable 0x00A90B38; null when there is no memory)
// for its children (0x004C4340). Meanwhile 0x00B21848 holds the walk
// 0x004D3B40, 0x00B21840 the occupant (referenced), 0x00B21844 0 and
// 0x00B2184C the window (referenced). Its children filled (0x004D3AE0), it
// runs modal (0x00B43CE0 slot 10 with 1, window manager slot 41, 0x00B43CE0
// slot 6). The globals cleared; answer 0: read back (0x004D3BD0), true.
// Then the painter taken off (0x004C4370), the runner's object cleared, the
// window taken off the manager (slot 23) and let go, the main window (view
// slot 3) given the focus (manager slot 37). With a kind kept: it is set
// and shown again and the timer stopped. The painter, the runner and the
// timer let go. Without a runner (no procedure part) the game calls through
// null; so does this.
uint8_t __cdecl DoQueryDialog(void* occupant)
{
    uint32_t instance = 0;
    void* const holder = Virtual<void*>(occupant, 0x18);
    if (!Cdecl<uint8_t>(addr::SC4_GetPropertyUint32, holder, kLayoutProperty, &instance))
        return 0;
    uint32_t key[3] = {0, tool_query::kLayoutGroup, instance};
    void* const window = Cdecl<void*>(addr::SC4UI_LoadWindowChild, static_cast<uint32_t*>(key),
                                      static_cast<void*>(nullptr), kLoadFlags);
    if (!window)
        return 0;
    void* const view = Global<void*>(addr::SC4_Service43DC4);
    const uint32_t kept = At<uint32_t>(Virtual<char*>(view, 0xC4), 0x14);
    const uint32_t marker = This<uint32_t>(addr::SC4ViewMarkers_F7D1AB0, Virtual<void*>(view, 0xC4), occupant);
    alignas(4) char timer[0x28];
    This<void*>(addr::cRZTimer_Construct, static_cast<void*>(timer));
    if (marker) {
        This<void>(addr::SC4ViewMarkers_F7D1D10, Virtual<void*>(view, 0xC4), marker);
        This<void>(addr::SC4ViewMarkers_F7D20E0, Virtual<void*>(view, 0xC4));
        This<uint8_t>(addr::cRZTimer_SetCallback, static_cast<void*>(timer), addr::SC4QueryTool_TimerSend,
                      uint32_t{0});
        This<uint8_t>(addr::cRZTimer_Start, static_cast<void*>(timer), kMarkerTicks, static_cast<void*>(nullptr));
    }

    Virtual<void>(window, 0x5C);
    char* runner = nullptr;
    void* procedure = nullptr;
    if (Virtual<uint8_t>(window, 0x0, kProcedureIid, &procedure)) {
        char* const memory = Cdecl<char*>(addr::SC4_operator_new, uint32_t{window_runner::kSize});
        char* const made = memory ? This<char*>(addr::SC4QueryWindowRunner_Construct, memory, procedure) : nullptr;
        This<void*>(addr::SC4RefPtr_Assign, static_cast<void*>(&runner), made);
        Virtual<void>(runner, 0x18, occupant);
        Virtual<void>(procedure, 0x1C, runner + window_runner::kProcedure);
        Release(procedure);
    }
    std::uintptr_t* painter = Cdecl<std::uintptr_t*>(addr::SC4_operator_new, uint32_t{4});
    if (painter) {
        *painter = addr::SC4QueryBarPainter_Vtable;
        AddRef(painter);
    }
    Virtual<uint8_t>(window, 0x80, kWindowIid, addr::SC4QueryTool_AttachChildren, static_cast<void*>(painter));
    Global<std::uintptr_t>(addr::SC4QueryTool_FillCellWalk) = addr::SC4QueryTool_FillChildrenNotEdits;
    AddRef(occupant);
    Global<void*>(addr::SC4QueryTool_FillCellQueries) = occupant;
    Global<void*>(addr::SC4QueryTool_FillCellQueries + 4) = nullptr;
    Global<void*>(addr::SC4QueryTool_FillCellWindow) = window;
    AddRef(window);
    Virtual<uint8_t>(window, 0x80, kWindowIid, addr::SC4QueryTool_FillChildren, occupant);

    void* const manager = Cdecl<void*>(addr::SC4_GetWinManager);
    Virtual<void>(Global<void*>(addr::SC4_Service43CE0), 0x28, window, uint32_t{1});
    const uint32_t answer = Virtual<uint32_t>(manager, 0xA4, window);
    Virtual<void>(Global<void*>(addr::SC4_Service43CE0), 0x18, window);
    Global<std::uintptr_t>(addr::SC4QueryTool_FillCellWalk) = 0;
    const uint8_t accepted = answer == 0;
    Release(occupant);
    Global<void*>(addr::SC4QueryTool_FillCellQueries) = nullptr;
    Global<void*>(addr::SC4QueryTool_FillCellQueries + 4) = nullptr;
    Release(window);
    Global<void*>(addr::SC4QueryTool_FillCellWindow) = nullptr;
    if (accepted)
        Virtual<uint8_t>(window, 0x80, kWindowIid, addr::SC4QueryTool_ReadChildren, occupant);

    Virtual<uint8_t>(window, 0x80, kWindowIid, addr::SC4QueryTool_DetachChildren, static_cast<void*>(painter));
    Virtual<void>(runner, 0x18, static_cast<void*>(nullptr));
    Virtual<void>(manager, 0x5C, window);
    Release(window);
    void* const main = Virtual<void*>(view, 0xC);
    Virtual<void>(manager, 0x94, main);
    if (kept) {
        This<void>(addr::SC4ViewMarkers_F7D1D10, Virtual<void*>(view, 0xC4), kept);
        This<void>(addr::SC4ViewMarkers_F7D20E0, Virtual<void*>(view, 0xC4));
        This<uint8_t>(addr::cRZTimer_Stop, static_cast<void*>(timer));
    }
    if (painter)
        Release(painter);
    Release(runner);
    This<void>(addr::cRZTimer_Destruct, static_cast<void*>(timer));
    return accepted;
}

// --- The window runner's procedure -------------------------------------------------------------

// The game's 0x004D3F40 (slot 3 of the runner's procedure part, vtable
// 0x00A90B58: window, message; ret 8; `self` is the runner's +0x8). Only
// for the runner's window (the procedure part at +0x10, its slot 3) and
// messages of type 3. 0x287259F6 with 0 or 1: the window ends with it
// (0x004C5690 on the runner), true. 0x887113A3 for a slider (the ID in the
// message, interface 0x21325207): its value (slot 4) hundredths of its cost
// (the property 0x2AA6E8AD, 64 bits; 0 without) truncated as money into the
// control 0x100 past it (0x004C6E00); with the property 0x6A8CC9D6 and an
// object at the runner's +0x14 that function is called (cdecl: window, the
// slider window's slot 63, 0, object), the place it is read into being the
// slider value's (what slot 52 does not write stays the value); with the
// property 0x0AB3F6DB each of its IDs (slot 103, count slot 4 asked each
// time) filled (0x004D3A90 with 1). The slider is let go right after its
// value is read and used after, as in the game. False for all but the end.
uint8_t __fastcall RunnerOnMessage(char* self, void*, void* window, const uint32_t* message)
{
    void* const procedure = At<void*>(self, window_runner::kWindow - window_runner::kProcedure);
    if (!procedure || window != Virtual<void*>(procedure, 0xC))
        return 0;
    if (message[window_message::kType / 4] != window_message::kCommand)
        return 0;
    const uint32_t what = message[window_message::kWhat / 4];
    if (what == window_message::kEnd) {
        const uint32_t code = message[window_message::kData / 4];
        if (code > 1)
            return 0;
        This<void>(addr::SC4QueryWindowRunner_End, self - window_runner::kProcedure, code);
        return 1;
    }
    if (what != window_message::kSliderMoved)
        return 0;
    const uint32_t id = message[window_message::kData / 4];
    void* slider = nullptr;
    if (!Virtual<uint8_t>(window, 0x94, id, kNumberControl, &slider))
        return 0;
    const int32_t value = Virtual<int32_t>(slider, 0x10);
    Release(slider);

    void* property = nullptr;
    int64_t cost = 0;
    if (Virtual<uint8_t>(Virtual<void*>(slider, 0xC), 0x1C4, kCostProperty, &property)) {
        cost = *Virtual<const int64_t*>(property, 0x13C);
        Release(property);
    }
    const double amount = static_cast<double>(value) * kHundredth * static_cast<double>(cost);
    Cdecl<uint8_t>(addr::SC4QueryTool_SetChildMoneyNext, window, id, Ftol64(amount));

    property = nullptr;
    if (Virtual<uint8_t>(Virtual<void*>(slider, 0xC), 0x1C4, kHandlerProperty, &property)) {
        std::uintptr_t handler = static_cast<uint32_t>(value);
        Virtual<void>(property, 0xD0, &handler);
        if (void* const about = At<void*>(self, window_runner::kObject - window_runner::kProcedure)) {
            const uint32_t control = Virtual<uint32_t>(Virtual<void*>(slider, 0xC), 0xFC);
            Cdecl<void>(handler, window, control, uint32_t{0}, about);
        }
        Release(property);
    }

    property = nullptr;
    if (Virtual<uint8_t>(Virtual<void*>(slider, 0xC), 0x1C4, kIdsProperty, &property)) {
        const uint32_t* const ids = Virtual<const uint32_t*>(property, 0x19C);
        for (uint32_t i = 0; i < Virtual<uint32_t>(property, 0x10); ++i)
            FillControl(window, ids[i], 1, At<void*>(self, window_runner::kObject - window_runner::kProcedure));
        Release(property);
    }
    return 0;
}

// --- Registration ----------------------------------------------------------------------

bool InstallQuery6a(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[28] = {};
    static const struct {
        std::uintptr_t address;
        void* ours;
    } hooks[] = {
        {addr::SC4QueryTool_NetworkTrafficInfo, reinterpret_cast<void*>(&NetworkTrafficInfo)},
        {addr::SC4QueryTool_SetChildMoneyFloat, reinterpret_cast<void*>(&SetChildMoneyFloat)},
        {addr::SC4QueryTool_FillVacateReason, reinterpret_cast<void*>(&FillVacateReason)},
        {addr::SC4QueryTool_FillPowerUnitCost, reinterpret_cast<void*>(&FillPowerUnitCost)},
        {addr::SC4QueryTool_FillWaterUnitCost, reinterpret_cast<void*>(&FillWaterUnitCost)},
        {addr::SC4QueryTool_LotWealth, reinterpret_cast<void*>(&LotWealth)},
        {addr::SC4QueryTool_DeveloperOfLot, reinterpret_cast<void*>(&DeveloperOfLot)},
        {addr::SC4QueryTool_OccupantLevelText, reinterpret_cast<void*>(&OccupantLevelText)},
        {addr::SC4QueryTool_FillWealthBar, reinterpret_cast<void*>(&FillWealthBar)},
        {addr::SC4QueryTool_FillLevelPollution, reinterpret_cast<void*>(&FillLevelPollution)},
        {addr::SC4QueryTool_FillLevelGarbage, reinterpret_cast<void*>(&FillLevelGarbage)},
        {addr::SC4QueryTool_FillLevelThird, reinterpret_cast<void*>(&FillLevelThird)},
        {addr::SC4QueryTool_FillLevelFifth, reinterpret_cast<void*>(&FillLevelFifth)},
        {addr::SC4QueryTool_FillLevelFourth, reinterpret_cast<void*>(&FillLevelFourth)},
        {addr::SC4QueryTool_FillLevelFirst, reinterpret_cast<void*>(&FillLevelFirst)},
        {addr::SC4QueryTool_FillLevelSecond, reinterpret_cast<void*>(&FillLevelSecond)},
        {addr::SC4QueryTool_FillControl, reinterpret_cast<void*>(&FillControl)},
        {addr::SC4QueryTool_FillChildren, reinterpret_cast<void*>(&FillChildren)},
        {addr::SC4QueryTool_FillChildrenNotEdits, reinterpret_cast<void*>(&FillChildrenNotEdits)},
        {addr::SC4QueryTool_ReadChildren, reinterpret_cast<void*>(&ReadChildren)},
        {addr::SC4QueryTool_DoQueryDialog, reinterpret_cast<void*>(&DoQueryDialog)},
        {addr::SC4QueryWindowRunner_OnMessage, reinterpret_cast<void*>(&RunnerOnMessage)},
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
    "ui.tools.query6a",
    "ui",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4QueryTool_NetworkTrafficInfo,
    "Our query tool, sixth piece without the cell log: network traffic texts, fillers, a lot's developer, the "
    "query dialog and the window runner's procedure (docs/tools.md)",
    false,
    InstallQuery6a,
});

} // namespace
} // namespace opensc4::tool_query_6a
