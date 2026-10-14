// The query tool of the city view, third piece, second half: 0x004C8E30 to
// 0x004CA45D.
// Replacement, off by default:
//
//   ui.tools.query3b: 22 functions of the query tool
//     (cSC4ViewInputControlQuery: a view input control of 0xA0 bytes, vtable
//     0x00A90A88; docs/tools.md): entries of the filler table at 0x00A92488
//     for lots (population, capacity, grade, level), budget items, the six
//     airport and seaport entries, power plants and water buildings (texts
//     from the game's strings), a text key property; three helpers of the
//     filler 0x004CC240 (a later piece: a neighbour's mayor and city name,
//     the traffic at a cell); and the desirability log line set of a tract
//     (0x004C9520, called by the cell log 0x004D12A0).
//
// Entries of the table that are functions of this file (index, address): 19
// 0x004C9E60, 49 and 56 0x004C9FF0, 64 0x004CA090, 74 0x004CA130 (code
// Ghidra has not defined: reached only through the table), 81 0x004C8FF0,
// 82 0x004C90A0, 86 0x004C9120, 88 0x004C9200, 92 0x004C92A0, 97 and 98
// 0x004CA040, 108 0x004C9310 (16 too, through the jump at 0x004C9E50), 118
// 0x004CA2C0, 119 0x004CA390, 120 0x004C9FA0, 123 0x004C9EA0, 124
// 0x004C9EE0, 125 0x004C9F20, 126 0x004C9F60. None of them changes the
// city.
//
// Left the game's: 0x004C9430 (a list's copy constructor, generic, a caller
// outside the tool at 0x0051A7D0), 0x004C9470 (SC4LotTravelMap's copy
// constructor, the lots' code), 0x004C9E50 and 0x004C9FE0 (five-byte jumps
// to 0x004C9310 and 0x004C8660, which are hooked).
//
// Nothing here reads a clock or draws a random number. The desirability log
// reads the simulator's day number (the simulator service's slot 7, its slot
// 26: sim time) and makes four static strings on its first use (the guard
// bit at 0x00B218E8, the exit handler 0x00A7B120), as the game does.
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"
#include "core/tool_query.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>

namespace opensc4::tool_query_3b {
namespace {

namespace developer = tool_query::developer;
namespace development = tool_query::development;

constexpr uint32_t kTextControl = 0x212CDC1F;
constexpr uint32_t kTextGroup = 0xEA5524EB;        // the group of the query's game strings
constexpr uint32_t kBudgetPurposes = 0xEA54D285;   // a building's budget purposes (uint32 list property)
constexpr uint32_t kTextKeyProperty = 0xAA5C23FA;  // a text key (type, group, instance)

// The floats at 0x00A80AA0 (100) and 0x00A90BBC (1 over 255); the float 2
// to the 32nd at 0x00A80AA8.
constexpr float kHundred = 100.0f;
constexpr float kOverByte = 0x1.010102p-8f;
constexpr double kTwoTo32 = 4294967296.0;

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

// __ftol (0x009EEF04): truncation toward zero into 64 bits, the low 32 bits
// kept; what is out of range or not a number gives 0, the low half of
// 0x8000000000000000, as the x87 does.
int32_t Ftol(double value)
{
    if (!(value > -9223372036854775808.0 && value < 9223372036854775808.0))
        return 0;
    return static_cast<int32_t>(static_cast<int64_t>(value));
}

// An unsigned dword as fild with the 2 to the 32nd added when negative.
double Unsigned(uint32_t value)
{
    double whole = static_cast<double>(static_cast<int32_t>(value));
    if (static_cast<int32_t>(value) < 0)
        whole += kTwoTo32;
    return whole;
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

// SC4LocalizedString: the string, then the key (type 0x2026960B, group,
// instance). 0x20 bytes.
struct LocalizedString {
    GameString text;
    uint32_t key[3];
};
static_assert(sizeof(LocalizedString) == 0x20);

// The game's inline construction of an empty string: an 8-byte buffer from
// the STL allocator holding a 0.
void MakeEmpty(GameString& text)
{
    text.begin = nullptr;
    text.end = nullptr;
    text.capacity = nullptr;
    char* const buffer = Cdecl<char*>(addr::SC4_stl_allocate, uint32_t{8});
    text.begin = buffer;
    text.end = buffer;
    text.capacity = buffer + 8;
    *buffer = 0;
    text.vtable = addr::SC4String_Vtable;
    text.zero = 0;
}

// The game's inline destructor (also of a localized string): the vtable
// set to the string's again, the buffer freed.
void Destroy(GameString& text)
{
    text.vtable = addr::SC4String_Vtable;
    if (text.begin)
        Cdecl<void>(addr::SC4_Free, static_cast<void*>(text.begin));
}

// A localized string of a group and instance, loaded (0x00603580 with 1).
void MakeLocalized(LocalizedString& text, uint32_t group, uint32_t instance)
{
    This<void*>(addr::SC4LocalizedString_SC4LocalizedString, &text, group, instance, uint32_t{1});
}

// --- The first two pieces' helpers, by address --------------------------------------------

template <typename... Args>
void Log(const char* format, Args... args)
{
    Cdecl<void>(addr::SC4QueryTool_Log, format, args...);
}

uint8_t SetChildText(void* window, uint32_t id, void* text)
{
    return Cdecl<uint8_t>(addr::SC4QueryTool_SetChildText, window, id, text);
}

uint8_t SetChildInt(void* window, uint32_t id, int32_t value)
{
    return Cdecl<uint8_t>(addr::SC4QueryTool_SetChildInt, window, id, value);
}

uint8_t SetChildPercent(void* window, uint32_t id, int32_t value)
{
    return Cdecl<uint8_t>(addr::SC4QueryTool_SetChildPercent, window, id, value);
}

uint8_t SetChildMoney(void* window, uint32_t id, int64_t amount)
{
    return Cdecl<uint8_t>(addr::SC4QueryTool_SetChildMoney, window, id, amount);
}

// What 0x004CA1E0, 0x004CA2C0 and 0x004CA390 do in line: the window's child
// as a text control (slot 37), its caption (slot 4) set to the text, let
// go. False without the window or the child.
uint8_t Caption(void* window, uint32_t id, void* text)
{
    void* control = nullptr;
    if (!window || !Virtual<uint8_t>(window, 0x94, id, kTextControl, &control))
        return 0;
    Virtual<void>(control, 0x10, text);
    Release(control);
    return 1;
}

// --- Neighbours and traffic: helpers of 0x004CC240 -------------------------------------------

// The neighbour city across the edge at a cell, as both neighbour helpers
// find it: the cell must lie just outside one edge of the city (0x006CE7A0
// with no city), the neighbours simulator (0x00B43D8C) must have a
// connection of the type there (slot 11) and a city across (slot 12). Null
// otherwise.
void* NeighbourAt(int32_t x, int32_t z, uint32_t type)
{
    void* const neighbours = Global<void*>(addr::SC4_MonthEndService);
    if (!Cdecl<uint8_t>(addr::SC4_IsOnOneCityEdge, x, z, static_cast<void*>(nullptr)))
        return nullptr;
    if (!Virtual<void*>(neighbours, 0x2C, type, x, z))
        return nullptr;
    return Virtual<void*>(neighbours, 0x30, x, z);
}

// The game's 0x004C8E30 (cdecl (window, ID, fill, cell x, cell z, type);
// one caller, 0x004CC240): with `fill`, the mayor's name of the neighbour
// city there (its slot 35) into the text control (0x004C3C00); false
// without one.
uint8_t __cdecl FillNeighbourMayor(void* window, uint32_t id, uint32_t fill, int32_t x, int32_t z, uint32_t type)
{
    if (!static_cast<uint8_t>(fill))
        return 1;
    void* const across = NeighbourAt(x, z, type);
    if (!across)
        return 0;
    GameString name;
    MakeEmpty(name);
    Virtual<void>(across, 0x8C, &name);
    const uint8_t done = SetChildText(window, id, &name);
    Destroy(name);
    return done;
}

// The game's 0x004CA1E0 (the same arguments and caller): with `fill`, the
// name of the neighbour city there (its slot 33) as the control's caption;
// without a city the game's string 0x0A8E15F9 of the group 0x6A231EAA (the
// name the game gives an empty tile) stays. The answer is whether the
// control took it.
uint8_t __cdecl FillNeighbourName(void* window, uint32_t id, uint32_t fill, int32_t x, int32_t z, uint32_t type)
{
    if (!static_cast<uint8_t>(fill))
        return 1;
    LocalizedString name;
    MakeLocalized(name, 0x6A231EAA, 0x0A8E15F9);
    if (void* const across = NeighbourAt(x, z, type))
        Virtual<void>(across, 0x84, &name);
    const uint8_t done = Caption(window, id, &name);
    Destroy(name.text);
    return done;
}

// The game's 0x004C8F20 (cdecl (window, ID, fill, x, z); the caller passes
// a sixth, not read): with `fill`, the cell held between 0 and the city's
// last cell (slots 93 and 94 less 1: below 0 is 0, from the last on the
// last), the traffic simulator's slot 10 map there (8 bits) over 255 in
// percent, truncated.
uint8_t __cdecl FillTrafficCellPercent(void* window, uint32_t id, uint32_t fill, int32_t x, int32_t z)
{
    if (!static_cast<uint8_t>(fill))
        return 1;
    void* const city = Global<void*>(addr::SC4_City);
    const int32_t lastX = Virtual<int32_t>(city, 0x174) - 1;
    const int32_t lastZ = Virtual<int32_t>(Global<void*>(addr::SC4_City), 0x178) - 1;
    int32_t cellX = x < 0 ? 0 : x;
    if (!(cellX < lastX))
        cellX = lastX;
    int32_t cellZ = z < 0 ? 0 : z;
    if (!(cellZ < lastZ))
        cellZ = lastZ;
    void* const map = Virtual<void*>(Global<void*>(addr::SC4_Service43DA8), 0x28);
    const uint8_t value = Virtual<uint8_t>(map, 0x1C, cellX, cellZ);
    const double share = static_cast<double>(value) * static_cast<double>(kOverByte);
    return SetChildPercent(window, id, Ftol(share * static_cast<double>(kHundred)));
}

// --- Lots -------------------------------------------------------------------------------

// The game's 0x004C8FF0 (table index 81): with `fill` and an occupant, how
// many are on its lot (0x004C53A0) and the lot's capacity (0x004C4160) as
// "%d/%d". True.
uint8_t __cdecl FillLotPopulationText(void* window, uint32_t id, uint32_t fill, void* occupant)
{
    if (!static_cast<uint8_t>(fill) || !occupant)
        return 1;
    const uint32_t population = Cdecl<uint32_t>(addr::SC4QueryTool_LotPopulation, occupant);
    const uint32_t capacity = Cdecl<uint32_t>(addr::SC4QueryTool_LotCapacity, occupant);
    GameString text;
    MakeEmpty(text);
    Cdecl<void>(addr::cRZBaseString_Sprintf, &text, "%d/%d", population, capacity);
    SetChildText(window, id, &text);
    Destroy(text);
    return 1;
}

// The game's 0x004C90A0 (table index 82): with `fill` and an occupant, the
// lot's capacity as text (a string made by 0x00578390), when it is not 0.
// True.
uint8_t __cdecl FillLotCapacityText(void* window, uint32_t id, uint32_t fill, void* occupant)
{
    if (!static_cast<uint8_t>(fill) || !occupant)
        return 1;
    const uint32_t capacity = Cdecl<uint32_t>(addr::SC4QueryTool_LotCapacity, occupant);
    if (!capacity)
        return 1;
    GameString text;
    This<void*>(addr::SC4String_SC4String, &text);
    Cdecl<void>(addr::cRZBaseString_Sprintf, &text, "%d", capacity);
    SetChildText(window, id, &text);
    Destroy(text);
    return 1;
}

// The game's 0x004C9120 (table index 86): with `fill` and an occupant, the
// text of the grade at its cell (0x004C4210): 2 the string 0x0A5FE260
// (Medium), 3 0xAA5FE249 (High), anything else 0x4A5FE266 (Low), of the
// group 0xEA5524EB (0x00603040). True.
uint8_t __cdecl FillCellGradeText(void* window, uint32_t id, uint32_t fill, void* occupant)
{
    if (!static_cast<uint8_t>(fill) || !occupant)
        return 1;
    const uint32_t grade = Cdecl<uint32_t>(addr::SC4QueryTool_CellGradeOf, occupant);
    GameString text;
    MakeEmpty(text);
    uint32_t instance = 0x4A5FE266;
    if (grade == 2)
        instance = 0x0A5FE260;
    else if (grade == 3)
        instance = 0xAA5FE249;
    Cdecl<uint8_t>(addr::SC4_LoadLocalizedText, &text, kTextGroup, instance, uint32_t{0});
    SetChildText(window, id, &text);
    Destroy(text);
    return 1;
}

// The game's 0x004C9200 (table index 88): with `fill` and an occupant, the
// level text (0x004C4260) of the lot's average (0x004C5490, rounded to a
// float). True.
uint8_t __cdecl FillLotAverageText(void* window, uint32_t id, uint32_t fill, void* occupant)
{
    if (!static_cast<uint8_t>(fill) || !occupant)
        return 1;
    GameString text;
    MakeEmpty(text);
    const float average = static_cast<float>(Cdecl<double>(addr::SC4QueryTool_LotAverage, occupant));
    Cdecl<uint32_t>(addr::SC4QueryTool_LevelText, static_cast<void*>(&text), average);
    SetChildText(window, id, &text);
    Destroy(text);
    return 1;
}

// The game's 0x004C92A0 (table index 92): with `fill` and an occupant, how
// many are on its lot over the lot's capacity (both unsigned) times 100,
// truncated, as a number (not a percentage). A capacity of 0 divides by 0
// as the x87 does: what comes out of the truncation is 0. True.
uint8_t __cdecl FillLotOccupancy(void* window, uint32_t id, uint32_t fill, void* occupant)
{
    if (!static_cast<uint8_t>(fill) || !occupant)
        return 1;
    const double population = Unsigned(Cdecl<uint32_t>(addr::SC4QueryTool_LotPopulation, occupant));
    const double capacity = Unsigned(Cdecl<uint32_t>(addr::SC4QueryTool_LotCapacity, occupant));
    SetChildInt(window, id, Ftol(population / capacity * static_cast<double>(kHundred)));
    return 1;
}

// --- Budget items --------------------------------------------------------------------------

// cISC4BudgetSimulator::BudgetItem as the game's code has it.
struct BudgetItem {
    uint32_t department;
    uint32_t lineItem;
    uint32_t purpose;
    uint32_t padding;
    int64_t cost;
    uint32_t index;
    float localFunding; // the one field the caller sets (0, once) before the calls
};
static_assert(sizeof(BudgetItem) == 0x20);

// An SC4Vector of uint32 as the property getter fills it.
struct UintVector {
    uint32_t* begin;
    uint32_t* end;
    uint32_t* capacity;
};

// The game's 0x004C9310 (table index 108; index 16 jumps here through
// 0x004C9E50): with `fill`, the occupant's budget purposes (its property
// 0xEA54D285 as a list, 0x005FF510; false without it) each looked up in the
// budget simulator (slot 51 with the occupant's property holder) and the
// costs of those found added up, as money. The list's memory is given back.
uint8_t __cdecl FillBudgetItemsCost(void* window, uint32_t id, uint32_t fill, void* occupant)
{
    if (!static_cast<uint8_t>(fill))
        return 1;
    BudgetItem item = {};
    UintVector purposes = {};
    if (!Cdecl<uint8_t>(addr::SC4_GetPropertyUint32Vector, Virtual<void*>(occupant, 0x18), kBudgetPurposes,
                        &purposes)) {
        if (purposes.begin)
            Cdecl<void>(addr::SC4_Free, static_cast<void*>(purposes.begin));
        return 0;
    }
    int64_t sum = 0;
    for (uint32_t i = 0; i < static_cast<uint32_t>(purposes.end - purposes.begin); ++i) {
        const uint32_t purpose = purposes.begin[i];
        void* const budget = Global<void*>(addr::SC4_BudgetSimulator);
        void* const holder = Virtual<void*>(occupant, 0x18);
        if (Virtual<uint8_t>(budget, 0xCC, holder, purpose, &item))
            sum += item.cost;
    }
    const uint8_t done = SetChildMoney(window, id, sum);
    if (purposes.begin)
        Cdecl<void>(addr::SC4_Free, static_cast<void*>(purposes.begin));
    return done;
}

// --- The desirability log -------------------------------------------------------------------

// A curve (0x005F4250) at a value, truncated.
int32_t Curved(void* curve, float at)
{
    return Ftol(This<double>(addr::SC4Curve_Evaluate, curve, at));
}

// One effect of a whole-number map: its value at the cell through the
// curve, logged with the value and added up.
void WholeEffect(const char* format, void* curve, int32_t value, int32_t& sum)
{
    const int32_t effect = Curved(curve, static_cast<float>(value));
    Log(format, value, effect);
    sum += effect;
}

// The same for a map of floats (logged with one decimal).
void FloatEffect(const char* format, void* curve, float value, int32_t& sum)
{
    const int32_t effect = Curved(curve, value);
    Log(format, static_cast<double>(value), effect);
    sum += effect;
}

void* Curve(char* object, std::size_t at)
{
    return At<void*>(object, at);
}

// The four names of the pollution types the log uses: game strings at
// 0x00B21898, 0x14 bytes apart, made on the first use (the guard's bit 0)
// with an exit handler that destroys them, as the game's function-local
// static does.
const char* PollutionName(uint32_t type)
{
    return At<const char*>(reinterpret_cast<void*>(addr::SC4QueryTool_PollutionNames + 0x14 * type), 4);
}

void MakePollutionNames()
{
    if (Global<uint8_t>(addr::SC4QueryTool_PollutionNamesGuard) & 1)
        return;
    Global<uint32_t>(addr::SC4QueryTool_PollutionNamesGuard) |= 1;
    static const char* const kNames[] = {"Air", "Water", "Garbage", "Radiation"};
    for (uint32_t type = 0; type < 4; ++type)
        This<void*>(addr::SC4String_FromText, reinterpret_cast<void*>(addr::SC4QueryTool_PollutionNames + 0x14 * type),
                    kNames[type]);
    Cdecl<int32_t>(addr::SC4_atexit, std::uintptr_t{addr::SC4QueryTool_PollutionNamesAtExit});
}

// cRZDate as the game keeps one on the stack: the vtable and the day.
struct Date {
    std::uintptr_t vtable;
    uint32_t day;
};

// The game's 0x004C9520 (cdecl (cell x, cell z, developer type); called by
// the cell log 0x004D12A0 for the types 0x1010, 0x1020, 0x1030 and so on):
// how the developer of the type (the development service's slot 32) works
// out the desirability of the tract at a cell, as log lines (0x004C3750):
// the base value, then for each effect whose curve the developer has the
// map's value at the cell and what the curve makes of it, added up (or the
// line saying it is off), the sum; then whether growth is locked down on
// the tract (the newest of the service's locks for the bytes 1 to 8, while
// fewer days have passed than a lock lasts: the day as text, 0x00912C79)
// and the tract's population, capacity, maximum capacity and desirability
// from the developer's tract maps. The tract is the cell shifted by what
// the desirability map's slot 11 gives. Only the log changes.
void __cdecl LogDesirability(int32_t x, int32_t z, uint32_t type)
{
    void* const service = Global<void*>(addr::SC4_Service43D68);
    char* const self = Virtual<char*>(service, 0x80, type);
    const uint32_t shift = Virtual<uint32_t>(At<void*>(self, developer::kDesirability), 0x2C);
    int32_t sum = At<int32_t>(self, developer::kBaseValue);
    const int32_t tractX = x >> (shift & 0x1F);
    const int32_t tractZ = z >> (shift & 0x1F);
    Log("|   desirability = base value: %d\n", sum);

    if (void* const curve = Curve(self, developer::kLandValueCurve)) {
        void* const landValue = Global<void*>(addr::SC4_Service43D88);
        const std::size_t slot = At<uint8_t>(self, developer::kLandValueGrid) ? 0x1C : 0x20;
        void* const map = Virtual<void*>(landValue, slot);
        WholeEffect("|   + land value effect, f(value: %d): %d\n", curve, Virtual<uint8_t>(map, 0x1C, x, z), sum);
    } else {
        Log("|   + land value effect (disabled)\n");
    }
    if (void* const curve = Curve(self, developer::kSlopeCurve)) {
        void* const map = Virtual<void*>(Global<void*>(addr::SC4_Service43D88), 0x24);
        FloatEffect("|   + slope effect, f(value: %.1f): %d\n", curve, Virtual<float>(map, 0x1C, x, z), sum);
    } else {
        Log("|   + slope effect (disabled)\n");
    }

    static const char* const kProximity[3][2] = {
        {"|   + R$ proximity effect, f(%d): %d\n", "|   + R$ proximity effect (disabled)\n"},
        {"|   + R$$ proximity effect, f(%d): %d\n", "|   + R$$ proximity effect (disabled)\n"},
        {"|   + R$$$ proximity effect, f(%d): %d\n", "|   + R$$$ proximity effect (disabled)\n"},
    };
    void* const residential = Global<void*>(addr::SC4_Service43DA4);
    for (uint32_t wealth = 1; wealth <= 3; ++wealth) {
        if (void* const curve = Curve(self, developer::kProximityCurves + 4 * (wealth - 1))) {
            void* const map = Virtual<void*>(residential, 0x14, wealth);
            WholeEffect(kProximity[wealth - 1][0], curve, Virtual<uint8_t>(map, 0x1C, x, z), sum);
        } else {
            Log(kProximity[wealth - 1][1]);
        }
    }

    static const char* const kCommercial[2][2] = {
        {"|   + Co$$ proximity effect, f(%d): %d\n", "|   + Co$$ proximity effect (disabled)\n"},
        {"|   + Co$$$ proximity effect, f(%d): %d\n", "|   + Co$$$ proximity effect (disabled)\n"},
    };
    void* const commercial = Global<void*>(addr::SC4_Service43D6C);
    for (uint32_t which = 0; which < 2; ++which) {
        if (void* const curve = Curve(self, developer::kCommercialCurves + 4 * which)) {
            void* const map = Virtual<void*>(commercial, 0x14, which ? uint32_t{0x3330} : uint32_t{0x3320});
            WholeEffect(kCommercial[which][0], curve, Virtual<uint8_t>(map, 0x1C, x, z), sum);
        } else {
            Log(kCommercial[which][1]);
        }
    }

    if (void* const curve = Curve(self, developer::kParkCurve)) {
        void* const map = Virtual<void*>(Global<void*>(addr::SC4_Service43D60), 0x38);
        WholeEffect("|   + park effect, f(%d): %d\n", curve,
                    static_cast<int16_t>(Virtual<uint16_t>(map, 0x1C, x, z)), sum);
    } else {
        Log("|   + park effect (disabled)\n");
    }

    MakePollutionNames();
    for (uint32_t pollution = 0; pollution < 4; ++pollution) {
        if (At<void**>(self, developer::kPollutionCurves)[pollution]) {
            void* const map = Virtual<void*>(Global<void*>(addr::SC4_PollutionSimulator), 0x14, pollution);
            const int32_t value = Virtual<uint16_t>(map, 0x1C, x, z);
            const int32_t effect =
                Curved(At<void**>(self, developer::kPollutionCurves)[pollution], static_cast<float>(value));
            Log("|   + %s pollution effect, f(%d): %d\n", PollutionName(pollution), value, effect);
            sum += effect;
        } else {
            Log("|   + %s pollution effect (disabled)\n", PollutionName(pollution));
        }
    }

    if (void* const curve = Curve(self, developer::kSchoolCurve)) {
        void* const map = Virtual<void*>(Global<void*>(addr::SC4_Service43DA4), 0x9C);
        FloatEffect("|   + school effect, f(%.1f): %d\n", curve, Virtual<float>(map, 0x1C, x, z), sum);
    } else {
        Log("|   + school effect (disabled)\n");
    }
    if (void* const curve = Curve(self, developer::kHospitalCurve)) {
        void* const map = Virtual<void*>(Global<void*>(addr::SC4_Service43DA4), 0xA0);
        FloatEffect("|   + hospital effect, f(%.1f): %d\n", curve, Virtual<float>(map, 0x1C, x, z), sum);
    } else {
        Log("|   + hospital effect (disabled)\n");
    }
    if (void* const curve = Curve(self, developer::kCrimeCurve)) {
        void* const map = Virtual<void*>(Global<void*>(addr::SC4_Service43D70), 0x14);
        WholeEffect("|   + crime effect, f(%d): %d\n", curve, Virtual<uint8_t>(map, 0x1C, x, z), sum);
    } else {
        Log("|   + crime effect (disabled)\n");
    }
    if (void* const curve = Curve(self, developer::kTrafficCurve)) {
        void* const map = Virtual<void*>(Global<void*>(addr::SC4_Service43DA8), 0x24);
        WholeEffect("|   + traffic effect, f(%d): %d\n", curve, Virtual<uint8_t>(map, 0x1C, x, z), sum);
    } else {
        Log("|   + traffic effect (disabled)\n");
    }
    if (void* const curve = Curve(self, developer::kTripCurve)) {
        void* const map = Virtual<void*>(Global<void*>(addr::SC4_Service43DA8), 0x34);
        uint8_t value = Virtual<uint8_t>(map, 0x1C, x, z);
        if (value == 0)
            value = 0xFF;
        WholeEffect("|   + trip length effect, f(%d): %d\n", curve, value, sum);
    } else {
        Log("|   + trip length effect (disabled)\n");
    }
    if (void* const curve = Curve(self, developer::kLandmarkCurve)) {
        void* const map = Virtual<void*>(Global<void*>(addr::SC4_Service43D60), 0x3C);
        WholeEffect("|   + landmark effect, f(%d): %d\n", curve,
                    static_cast<int16_t>(Virtual<uint16_t>(map, 0x1C, x, z)), sum);
    } else {
        Log("|   + landmark effect (disabled)\n");
    }
    if (void* const curve = Curve(self, developer::kTransientCurve)) {
        void* const map = Virtual<void*>(Global<void*>(addr::SC4_Service43D60), 0x34);
        WholeEffect("|   + transient effect, f(%d): %d\n", curve, Virtual<uint8_t>(map, 0x1C, x, z), sum);
    } else {
        Log("|   + Transient effect (disabled)\n");
    }
    Log("|   = %d\n", sum);

    const int32_t capacity = Virtual<uint16_t>(At<void*>(self, developer::kCapacity), 0x58, tractX, tractZ);
    uint32_t locked = 0;
    for (uint32_t kind = 1; kind <= 8; ++kind) {
        uint32_t key[4] = {static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(self)),
                           static_cast<uint32_t>(tractX), static_cast<uint32_t>(tractZ), kind};
        char* const locks = static_cast<char*>(service) + development::kLocks;
        char* const node = This<char*>(addr::SC4BuildingCellHash_Find, locks, static_cast<uint32_t*>(key));
        if (node)
            locked = At<uint32_t>(node, 0x14);
    }
    if (locked) {
        const uint32_t today = Virtual<uint32_t>(Virtual<void*>(Global<void*>(addr::SC4_SimulatorService), 0x1C), 0x68);
        if (today - locked < At<uint32_t>(service, development::kLockDays)) {
            Date date = {addr::cRZDate_vftable, locked};
            GameString text;
            MakeEmpty(text);
            This<void*>(addr::cRZDate_ToString, &date, &text, uint32_t{0});
            Log("|   \n");
            Log("|   Growth was locked down on %s for %d days\n", static_cast<const char*>(text.begin),
                At<uint32_t>(service, development::kLockDays));
            Destroy(text);
        }
    }
    Log("|   \n");
    Log("| As calculated last cycle for tract (%d, %d)\n", tractX, tractZ);
    Log("|   Population:        %d\n",
        static_cast<int32_t>(static_cast<int16_t>(
            Virtual<uint16_t>(At<void*>(self, developer::kPopulation), 0x58, tractX, tractZ))));
    Log("|   Existing capacity: %d\n", capacity);
    Log("|   Maximum capacity:  %d\n",
        static_cast<int32_t>(Virtual<uint16_t>(At<void*>(self, developer::kMaxCapacity), 0x58, tractX, tractZ)));
    Log("|   Desirability:      %d\n",
        static_cast<int32_t>(static_cast<int16_t>(
            Virtual<uint16_t>(At<void*>(self, developer::kDesirability), 0x58, tractX, tractZ))));
}

// --- Airports and seaports ------------------------------------------------------------------

// What the six share: false without the occupant; else the second piece's
// helper with the first cell of the occupant's box (its slot 11).
uint8_t PortOf(void* window, uint32_t id, uint32_t fill, void* occupant, std::uintptr_t helper)
{
    if (!occupant)
        return 0;
    int32_t box[4];
    Virtual<void>(occupant, 0x2C, static_cast<int32_t*>(box));
    return Cdecl<uint8_t>(helper, window, id, fill, box[0], box[1]);
}

// The game's 0x004C9E60 (table index 19): the airport's cost (0x004C7690).
uint8_t __cdecl FillOccupantAirportCost(void* window, uint32_t id, uint32_t fill, void* occupant)
{
    return PortOf(window, id, fill, occupant, addr::SC4QueryTool_FillAirportCost);
}

// The game's 0x004C9EA0 (table index 123): the airport's first usage
// number (0x004C76F0).
uint8_t __cdecl FillOccupantAirportUsageFirst(void* window, uint32_t id, uint32_t fill, void* occupant)
{
    return PortOf(window, id, fill, occupant, addr::SC4QueryTool_FillAirportUsageFirst);
}

// The game's 0x004C9EE0 (table index 124): the airport's second (0x004C7760).
uint8_t __cdecl FillOccupantAirportUsageSecond(void* window, uint32_t id, uint32_t fill, void* occupant)
{
    return PortOf(window, id, fill, occupant, addr::SC4QueryTool_FillAirportUsageSecond);
}

// The game's 0x004C9F20 (table index 125): the seaport's first (0x004C77D0).
uint8_t __cdecl FillOccupantSeaportUsageFirst(void* window, uint32_t id, uint32_t fill, void* occupant)
{
    return PortOf(window, id, fill, occupant, addr::SC4QueryTool_FillSeaportUsageFirst);
}

// The game's 0x004C9F60 (table index 126): the seaport's second (0x004C7840).
uint8_t __cdecl FillOccupantSeaportUsageSecond(void* window, uint32_t id, uint32_t fill, void* occupant)
{
    return PortOf(window, id, fill, occupant, addr::SC4QueryTool_FillSeaportUsageSecond);
}

// The game's 0x004C9FA0 (table index 120): the seaport's cost (0x004C78B0).
uint8_t __cdecl FillOccupantSeaportCost(void* window, uint32_t id, uint32_t fill, void* occupant)
{
    return PortOf(window, id, fill, occupant, addr::SC4QueryTool_FillSeaportCost);
}

// --- Power plants and water buildings --------------------------------------------------------

void* Power()
{
    return Global<void*>(addr::SC4_Service43DA0);
}

void* Plumbing()
{
    return Global<void*>(addr::SC4_Service43D94);
}

// The game's 0x004C9FF0 (table indexes 49 and 56): with `fill`, the second
// of the power simulator's slot 33 numbers (both 0 before: what the plant
// uses of what it produces) as text.
uint8_t __cdecl FillPowerUsed(void* window, uint32_t id, uint32_t fill, void* about)
{
    if (!static_cast<uint8_t>(fill))
        return 1;
    uint32_t produced = 0, used = 0;
    Virtual<uint8_t>(Power(), 0x84, about, &produced, &used);
    return SetChildInt(window, id, static_cast<int32_t>(used));
}

// The game's 0x004CA040 (table indexes 97 and 98): with `fill`, the plant's
// condition (slot 46: an SC4Percentage returned through the place given
// first, then the plant) times 100, truncated, as a percentage. The game's
// place is the `fill` argument's (not cleared).
uint8_t __cdecl FillPlantCondition(void* window, uint32_t id, uint32_t fill, void* about)
{
    if (!static_cast<uint8_t>(fill))
        return 1;
    uint32_t place = fill;
    Virtual<float*>(Power(), 0xB8, &At<float>(&place, 0), about);
    return SetChildPercent(window, id, Ftol(static_cast<double>(At<float>(&place, 0)) * static_cast<double>(kHundred)));
}

// The game's 0x004CA090 (table index 64): with `fill`, the flag byte of the
// plumbing simulator's slot 24 (cleared before; false when it says no) as
// the game's string 0x0A5D4F43 when set, 0x2A5D4F3B when not (group
// 0xEA5524EB), into the text control. The answer is the control's.
uint8_t __cdecl FillWaterFlagText(void* window, uint32_t id, uint32_t fill, void* about)
{
    if (!static_cast<uint8_t>(fill))
        return 1;
    uint8_t flag = 0;
    if (!Virtual<uint8_t>(Plumbing(), 0x60, about, static_cast<uint32_t*>(nullptr), static_cast<uint32_t*>(nullptr),
                          static_cast<uint32_t*>(nullptr), &flag, static_cast<uint32_t*>(nullptr)))
        return 0;
    LocalizedString text;
    MakeLocalized(text, kTextGroup, flag ? 0x0A5D4F43 : 0x2A5D4F3B);
    const uint8_t done = SetChildText(window, id, &text);
    Destroy(text.text);
    return done;
}

// The game's 0x004CA130 (table index 74; Ghidra has no function there):
// with `fill`, the text whose key is the occupant's property 0xAA5C23FA
// (type, group, instance; 0x005FD5B0, all 0 before; false without it) into
// the text control. The occupant is not checked.
uint8_t __cdecl FillPropertyText(void* window, uint32_t id, uint32_t fill, void* occupant)
{
    if (!static_cast<uint8_t>(fill))
        return 1;
    uint32_t key[3] = {0, 0, 0};
    if (!Cdecl<uint8_t>(addr::SC4_GetPropertyResourceKey, Virtual<void*>(occupant, 0x18), kTextKeyProperty,
                        static_cast<uint32_t*>(key)))
        return 0;
    LocalizedString text;
    MakeLocalized(text, key[1], key[2]);
    const uint8_t done = SetChildText(window, id, &text);
    Destroy(text.text);
    return done;
}

// What 0x004CA2C0 and 0x004CA390 share: with `fill`, a localized string
// made empty (0x00603000), then the simulator asked about the building
// (the slot given, one argument), then the game's string 0xCA5D4F33 when
// the answer is true and 0x6A5D4F37 when not (group 0xEA5524EB) made and
// given to it (0x006035E0) and destroyed; the string as the control's
// caption. The answer is whether the control took it.
uint8_t YesNo(void* window, uint32_t id, uint32_t fill, void* about, void* simulator, std::size_t slot)
{
    if (!static_cast<uint8_t>(fill))
        return 1;
    LocalizedString shown;
    This<void*>(addr::SC4LocalizedString_ConstructBase, &shown);
    const bool yes = Virtual<uint8_t>(simulator, slot, about) != 0;
    LocalizedString made;
    MakeLocalized(made, kTextGroup, yes ? 0xCA5D4F33 : 0x6A5D4F37);
    This<void*>(addr::SC4LocalizedString_Assign, &shown, &made);
    Destroy(made.text);
    const uint8_t done = Caption(window, id, &shown);
    Destroy(shown.text);
    return done;
}

// The game's 0x004CA2C0 (table index 118): whether the building has power
// (the power simulator's slot 31).
uint8_t __cdecl FillPoweredText(void* window, uint32_t id, uint32_t fill, void* about)
{
    return YesNo(window, id, fill, about, Power(), 0x7C);
}

// The game's 0x004CA390 (table index 119): whether it has water (the
// plumbing simulator's slot 9).
uint8_t __cdecl FillWateredText(void* window, uint32_t id, uint32_t fill, void* about)
{
    return YesNo(window, id, fill, about, Plumbing(), 0x24);
}

// --- Registration ----------------------------------------------------------------------

bool InstallQuery3b(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[30] = {};
    static const struct {
        std::uintptr_t address;
        void* ours;
    } hooks[] = {
        {addr::SC4QueryTool_FillNeighbourMayor, reinterpret_cast<void*>(&FillNeighbourMayor)},
        {addr::SC4QueryTool_FillTrafficCellPercent, reinterpret_cast<void*>(&FillTrafficCellPercent)},
        {addr::SC4QueryTool_FillLotPopulationText, reinterpret_cast<void*>(&FillLotPopulationText)},
        {addr::SC4QueryTool_FillLotCapacityText, reinterpret_cast<void*>(&FillLotCapacityText)},
        {addr::SC4QueryTool_FillCellGradeText, reinterpret_cast<void*>(&FillCellGradeText)},
        {addr::SC4QueryTool_FillLotAverageText, reinterpret_cast<void*>(&FillLotAverageText)},
        {addr::SC4QueryTool_FillLotOccupancy, reinterpret_cast<void*>(&FillLotOccupancy)},
        {addr::SC4QueryTool_FillBudgetItemsCost, reinterpret_cast<void*>(&FillBudgetItemsCost)},
        {addr::SC4QueryTool_LogDesirability, reinterpret_cast<void*>(&LogDesirability)},
        {addr::SC4QueryTool_FillOccupantAirportCost, reinterpret_cast<void*>(&FillOccupantAirportCost)},
        {addr::SC4QueryTool_FillOccupantAirportUsageFirst, reinterpret_cast<void*>(&FillOccupantAirportUsageFirst)},
        {addr::SC4QueryTool_FillOccupantAirportUsageSecond, reinterpret_cast<void*>(&FillOccupantAirportUsageSecond)},
        {addr::SC4QueryTool_FillOccupantSeaportUsageFirst, reinterpret_cast<void*>(&FillOccupantSeaportUsageFirst)},
        {addr::SC4QueryTool_FillOccupantSeaportUsageSecond, reinterpret_cast<void*>(&FillOccupantSeaportUsageSecond)},
        {addr::SC4QueryTool_FillOccupantSeaportCost, reinterpret_cast<void*>(&FillOccupantSeaportCost)},
        {addr::SC4QueryTool_FillPowerUsed, reinterpret_cast<void*>(&FillPowerUsed)},
        {addr::SC4QueryTool_FillPlantCondition, reinterpret_cast<void*>(&FillPlantCondition)},
        {addr::SC4QueryTool_FillWaterFlagText, reinterpret_cast<void*>(&FillWaterFlagText)},
        {addr::SC4QueryTool_FillPropertyText, reinterpret_cast<void*>(&FillPropertyText)},
        {addr::SC4QueryTool_FillNeighbourName, reinterpret_cast<void*>(&FillNeighbourName)},
        {addr::SC4QueryTool_FillPoweredText, reinterpret_cast<void*>(&FillPoweredText)},
        {addr::SC4QueryTool_FillWateredText, reinterpret_cast<void*>(&FillWateredText)},
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
    "ui.tools.query3b",
    "ui",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4QueryTool_FillLotPopulationText,
    "Our query tool, third piece, second half: the fillers of lots, budget items, ports, power and water texts, the "
    "neighbour helpers and the desirability log (docs/tools.md)",
    false,
    InstallQuery3b,
});

} // namespace
} // namespace opensc4::tool_query_3b
