// The query tool of the city view, third piece, first half: 0x004C7A70 to
// 0x004C8E2E.
// Replacement, off by default:
//
//   ui.tools.query3a: 33 functions of the query tool
//     (cSC4ViewInputControlQuery: a view input control of 0xA0 bytes, vtable
//     0x00A90A88; docs/tools.md): entries of the filler table at 0x00A92488
//     for schools and hospitals, garbage buildings, power plants, water
//     buildings, transit switches and budget items, and two helpers of the
//     filler 0x004CC240 (a later piece).
//
// Entries of the table that are functions of this file (index, address):
// 24 0x004C7A70, 25 0x004C7BB0, 26 and 35 0x004C8160, 27, 29, 31 and 36
// 0x004C7CE0, 28 and 30 0x004C7D30, 32 0x004C7E10, 33 0x004C7F00, 34
// 0x004C8030, 37 0x004C81B0, 42 0x004C8330, 43 0x004C83B0, 44 0x004C8420, 47
// and 54 0x004C86D0, 48 and 55 0x004C8710, 51 0x004C85F0, 53 0x004C8660 (46
// too, through the jump at 0x004C9FE0), 59 0x004C8880, 60 0x004C8900, 61
// 0x004C8950, 62 0x004C89A0, 66 0x004C8A60, 67 0x004C8B30, 68 0x004C8BC0,
// 69 0x004C8C50, 70 0x004C8CE0, 71 0x004C8D40, 73 0x004C8DB0, 95
// 0x004C8470, 96 0x004C8550, 99 0x004C8760, 115 0x004C89F0. Five of them
// change the city when a window reads its control back (fill 0): 24, 25,
// 33 and 34 set a school's or a hospital's funding (the residential
// simulator's slot 19), 99 a power plant's (the power simulator's slot 51).
//
// Nothing is left the game's in this half. Nothing reads a clock or draws a
// random number.
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"
#include "core/tool_query.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>

namespace opensc4::tool_query_3a {
namespace {

namespace school_record = tool_query::school_record;
namespace transit_record = tool_query::transit_record;

constexpr uint32_t kNumberControl = 0x21325207;
constexpr uint32_t kSecondNumberProperty = 0x27812851;
constexpr uint32_t kBudgetPurpose = 0xAA59670C;    // the purpose a building's budget item is found by
constexpr uint32_t kTransportDepartment = 0xC921CEEB;

// The floats at 0x00A823A4 (0.01), 0x00A80AA0 (100), 0x00A90978 (1.5 times
// 2 to the 23rd: adding it and taking 0x4B400000 from the bits rounds to a
// whole number) and 0x00A81228 (1); the float 2 to the 32nd at 0x00A80AA8.
constexpr float kHundredth = 0.01f;
constexpr float kHundred = 100.0f;
constexpr float kRoundMagic = 12582912.0f;
constexpr uint32_t kRoundBias = 0xB4C00000;
constexpr float kOne = 1.0f;
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

uint32_t Bits(float value)
{
    return At<uint32_t>(&value, 0);
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

// A share as the sliders take it: times 100, rounded to the nearest whole
// number by the float trick.
int32_t Percent(float share)
{
    const double scaled = static_cast<double>(share) * static_cast<double>(kHundred);
    const float shifted = static_cast<float>(scaled + static_cast<double>(kRoundMagic));
    return static_cast<int32_t>(Bits(shifted) + kRoundBias);
}

// --- The game's string -------------------------------------------------------------------

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

// The game's inline destructor: the vtable set again, the buffer freed.
void Destroy(GameString& text)
{
    text.vtable = addr::SC4String_Vtable;
    if (text.begin)
        Cdecl<void>(addr::SC4_Free, static_cast<void*>(text.begin));
}

// --- The first two pieces' helpers, by address --------------------------------------------

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

uint8_t SetChildSlider(void* window, uint32_t id, int32_t value, int64_t cost, void* about, std::uintptr_t handler,
                       const uint32_t* ids, uint32_t count)
{
    return Cdecl<uint8_t>(addr::SC4QueryTool_SetChildSlider, window, id, int32_t{1}, int32_t{0}, value, cost, about,
                          handler, ids, count);
}

// --- Schools and hospitals (the residential simulator, 0x00B43DA4) ----------------------------

// The school record as the fillers keep it on the stack: the string made
// empty, +0x28, +0x2C and +0x34 set to 0 (the rest is the simulator's to
// fill; 0 here where the game has what was on the stack).
struct SchoolRecord {
    GameString text;
    uint32_t word[11]; // +0x14 to +0x3F
};
static_assert(sizeof(SchoolRecord) == school_record::kSize);

void MakeRecord(SchoolRecord& record)
{
    record = {};
    MakeEmpty(record.text);
}

template <typename T>
T& Field(SchoolRecord& record, std::size_t offset)
{
    return At<T>(&record, offset);
}

// What the four funding sliders share. The simulator's slot 26 (school) or
// 27 (hospital) for the occupant; false when it says no. With `fill` the
// share at `share` (+0x2C or +0x34), times 100 rounded, is the slider's
// value, the dword after it its cost (taken unsigned), the filler's own
// address the function the slider calls, and `ids` and the dword at
// `count` (0x00B096EC or 0x00B096F8, read now) the table it keeps; true
// whatever the slider says. Without it the slider's number (0x004C3C50;
// unset without the window or the child, 0 here) times 0.01 is stored at
// `share` and both shares go to the simulator's slot 19 with `school`: the
// building's funding changes. True.
uint8_t Funding(void* window, uint32_t id, uint32_t fill, void* about, std::size_t query, std::size_t share,
                std::uintptr_t self, const uint32_t* ids, std::uintptr_t count, uint32_t school)
{
    SchoolRecord record;
    MakeRecord(record);
    void* const simulator = Global<void*>(addr::SC4_Service43DA4);
    if (!Virtual<uint8_t>(simulator, query, about, &record)) {
        Destroy(record.text);
        return 0;
    }
    if (static_cast<uint8_t>(fill)) {
        const uint32_t many = count ? Global<uint32_t>(count) : 0;
        const uint32_t cost = Field<uint32_t>(record, share + 4);
        SetChildSlider(window, id, Percent(Field<float>(record, share)), static_cast<int64_t>(cost), about, self, ids,
                       many);
    } else {
        int32_t number = 0;
        Cdecl<uint8_t>(addr::SC4QueryTool_GetChildNumber, window, id, &number);
        Field<float>(record, share) =
            static_cast<float>(static_cast<double>(number) * static_cast<double>(kHundredth));
        float* const first = &Field<float>(record, school_record::kShare);
        float* const second = &Field<float>(record, school_record::kShareSecond);
        Virtual<uint8_t>(Global<void*>(addr::SC4_Service43DA4), 0x4C, about, first, second, school);
    }
    Destroy(record.text);
    return 1;
}

// The game's 0x004C7A70 (table index 24): a school's first funding share,
// with the table of four IDs at 0x00B096DC.
uint8_t __cdecl FillSchoolFunding(void* window, uint32_t id, uint32_t fill, void* about)
{
    return Funding(window, id, fill, about, 0x68, school_record::kShare, addr::SC4QueryTool_FillSchoolFunding,
                   reinterpret_cast<const uint32_t*>(addr::SC4QueryTool_SchoolSliderIds),
                   addr::SC4QueryTool_SchoolSliderIdCount, 1);
}

// The game's 0x004C7BB0 (table index 25): a school's second share, no
// table.
uint8_t __cdecl FillSchoolFundingSecond(void* window, uint32_t id, uint32_t fill, void* about)
{
    return Funding(window, id, fill, about, 0x68, school_record::kShareSecond,
                   addr::SC4QueryTool_FillSchoolFundingSecond, nullptr, 0, 1);
}

// The game's 0x004C7F00 (table index 33): a hospital's first share, with
// the table at 0x00B096F0 (the count the game reads, 0x00B096F8, holds 4).
uint8_t __cdecl FillHospitalFunding(void* window, uint32_t id, uint32_t fill, void* about)
{
    return Funding(window, id, fill, about, 0x6C, school_record::kShare, addr::SC4QueryTool_FillHospitalFunding,
                   reinterpret_cast<const uint32_t*>(addr::SC4QueryTool_HospitalSliderIds),
                   addr::SC4QueryTool_HospitalSliderIdCount, 0);
}

// The game's 0x004C8030 (table index 34): a hospital's second share.
uint8_t __cdecl FillHospitalFundingSecond(void* window, uint32_t id, uint32_t fill, void* about)
{
    return Funding(window, id, fill, about, 0x6C, school_record::kShareSecond,
                   addr::SC4QueryTool_FillHospitalFundingSecond, nullptr, 0, 0);
}

// What 0x004C7CE0 and 0x004C8160 share: the simulator's slot 28 (the
// occupant, two numbers out; asked with or without `fill`, its answer not
// looked at), then with `fill` one of the numbers as text. The game's two
// places are not set before the call (0 here). True without `fill`.
uint8_t Capacity(void* window, uint32_t id, uint32_t fill, void* about, bool second)
{
    uint32_t first = 0, other = 0;
    Virtual<uint8_t>(Global<void*>(addr::SC4_Service43DA4), 0x70, about, &first, &other);
    if (!static_cast<uint8_t>(fill))
        return 1;
    return SetChildInt(window, id, static_cast<int32_t>(second ? other : first));
}

// The game's 0x004C7CE0 (table indexes 27, 29, 31 and 36): the first.
uint8_t __cdecl FillCapacityFirst(void* window, uint32_t id, uint32_t fill, void* about)
{
    return Capacity(window, id, fill, about, false);
}

// The game's 0x004C8160 (table indexes 26 and 35): the second.
uint8_t __cdecl FillCapacitySecond(void* window, uint32_t id, uint32_t fill, void* about)
{
    return Capacity(window, id, fill, about, true);
}

// What 0x004C7D30 and 0x004C81B0 share: the record (slot 26 or 27; false
// when it says no, asked with or without `fill`), then with `fill` the
// number at +0x24 as text. True without `fill`.
uint8_t RecordNumber(void* window, uint32_t id, uint32_t fill, void* about, std::size_t query)
{
    SchoolRecord record;
    MakeRecord(record);
    if (!Virtual<uint8_t>(Global<void*>(addr::SC4_Service43DA4), query, about, &record)) {
        Destroy(record.text);
        return 0;
    }
    uint8_t done = 1;
    if (static_cast<uint8_t>(fill))
        done = SetChildInt(window, id, Field<int32_t>(record, school_record::kNumber));
    Destroy(record.text);
    return done;
}

// The game's 0x004C7D30 (table indexes 28 and 30): a school's.
uint8_t __cdecl FillSchoolNumber(void* window, uint32_t id, uint32_t fill, void* about)
{
    return RecordNumber(window, id, fill, about, 0x68);
}

// The game's 0x004C81B0 (table index 37): a hospital's.
uint8_t __cdecl FillHospitalNumber(void* window, uint32_t id, uint32_t fill, void* about)
{
    return RecordNumber(window, id, fill, about, 0x6C);
}

// The game's 0x004C7E10 (table index 32): with `fill`, a hospital's record
// (slot 27) and the float at its +0x28 as the control's value (0x004C3CA0).
// An unused empty 8-byte buffer is taken and given back on the way. False
// when the simulator says no.
uint8_t __cdecl FillHospitalValue(void* window, uint32_t id, uint32_t fill, void* about)
{
    if (!static_cast<uint8_t>(fill))
        return 1;
    SchoolRecord record;
    MakeRecord(record);
    char* const unused = Cdecl<char*>(addr::SC4_stl_allocate, uint32_t{8});
    *unused = 0;
    if (!Virtual<uint8_t>(Global<void*>(addr::SC4_Service43DA4), 0x6C, about, &record)) {
        Cdecl<void>(addr::SC4_Free, static_cast<void*>(unused));
        Destroy(record.text);
        return 0;
    }
    const uint8_t done = Cdecl<uint8_t>(addr::SC4QueryTool_SetChildValue, window, id,
                                        Field<uint32_t>(record, school_record::kValue));
    Cdecl<void>(addr::SC4_Free, static_cast<void*>(unused));
    Destroy(record.text);
    return done;
}

// --- Garbage (the pollution simulator, 0x00B43D9C) -------------------------------------------

void* Pollution()
{
    return Global<void*>(addr::SC4_PollutionSimulator);
}

// The game's 0x004C8290 (cdecl (window, ID, fill, x, z); the cell is not
// read; one caller, 0x004CC240): with `fill`, the garbage landfilled last
// month (slot 41, 64 bits, rounded to a float) times the scaling factor
// (slot 48), truncated, as text.
uint8_t __cdecl FillGarbageLandfilled(void* window, uint32_t id, uint32_t fill, int32_t, int32_t)
{
    if (!static_cast<uint8_t>(fill))
        return 1;
    const float landfilled = static_cast<float>(Virtual<int64_t>(Pollution(), 0xA4));
    const float scale = Virtual<float>(Pollution(), 0xC0);
    return SetChildInt(window, id, Ftol(static_cast<double>(scale) * static_cast<double>(landfilled)));
}

// The game's 0x004C82F0 (the same arguments and caller): with `fill`, the
// landfill developer's (the city's slot 78) current budget (slot 8, 64 bits)
// as money.
uint8_t __cdecl FillLandfillCost(void* window, uint32_t id, uint32_t fill, int32_t, int32_t)
{
    if (!static_cast<uint8_t>(fill))
        return 1;
    void* const developer = Virtual<void*>(Global<void*>(addr::SC4_City), 0x138);
    return SetChildMoney(window, id, Virtual<int64_t>(developer, 0x20));
}

// The game's 0x004C8330 (table index 42): with `fill`, an incinerator's
// used capacity (slot 58) over its current capacity (slot 60), both 0
// before, as a percentage in 32-bit unsigned arithmetic; 0 without a
// capacity.
uint8_t __cdecl FillIncineratorPercent(void* window, uint32_t id, uint32_t fill, void* about)
{
    if (!static_cast<uint8_t>(fill))
        return 1;
    uint32_t used = 0, capacity = 0;
    Virtual<uint8_t>(Pollution(), 0xE8, about, &used);
    Virtual<uint8_t>(Pollution(), 0xF0, about, &capacity);
    const uint32_t percent = capacity ? used * 100u / capacity : 0;
    return SetChildPercent(window, id, static_cast<int32_t>(percent));
}

// What 0x004C83B0 and 0x004C85F0 share: with `fill`, a used capacity (the
// slot given, 0 before; unsigned) times the scaling factor, truncated, as
// text.
uint8_t ScaledUse(void* window, uint32_t id, uint32_t fill, void* about, std::size_t slot)
{
    if (!static_cast<uint8_t>(fill))
        return 1;
    uint32_t used = 0;
    Virtual<uint8_t>(Pollution(), slot, about, &used);
    const float scale = Virtual<float>(Pollution(), 0xC0);
    return SetChildInt(window, id, Ftol(static_cast<double>(scale) * Unsigned(used)));
}

// The game's 0x004C83B0 (table index 43): an incinerator's (slot 58).
uint8_t __cdecl FillIncineratorUsed(void* window, uint32_t id, uint32_t fill, void* about)
{
    return ScaledUse(window, id, fill, about, 0xE8);
}

// The game's 0x004C85F0 (table index 51): a waste-to-energy plant's (slot
// 59).
uint8_t __cdecl FillWasteToEnergyUsed(void* window, uint32_t id, uint32_t fill, void* about)
{
    return ScaledUse(window, id, fill, about, 0xEC);
}

// The game's 0x004C8420 (table index 44): with `fill`, the first number of
// the property 0x27812851 of the occupant's property holder (the holder's
// slot 9; the property's value, slot 5; its numbers, slot 105) as text.
// False without the property; the occupant is not checked.
uint8_t __cdecl FillPropertyFirst(void* window, uint32_t id, uint32_t fill, void* occupant)
{
    if (!static_cast<uint8_t>(fill))
        return 1;
    void* const property = Virtual<void*>(Virtual<void*>(occupant, 0x18), 0x24, kSecondNumberProperty);
    if (!property)
        return 0;
    const uint32_t* const numbers = Virtual<const uint32_t*>(Virtual<void*>(property, 0x14), 0x1A4);
    return SetChildInt(window, id, static_cast<int32_t>(numbers[0]));
}

// The game's 0x004C8470 (table index 95): with `fill`, the share of last
// month's garbage that was recycled (slot 38 over slot 35, both 64 bits; 0
// when nothing was produced; rounded to a float) over the recycling
// centre's current capacity (slot 55, a float, 0 before), at most 1, as a
// percentage; 0 (the float at 0x00A81054) when the capacity is not above
// the float at 0x00B21854.
uint8_t __cdecl FillRecycledShare(void* window, uint32_t id, uint32_t fill, void* about)
{
    if (!static_cast<uint8_t>(fill))
        return 1;
    const int64_t produced = Virtual<int64_t>(Pollution(), 0x8C);
    const int64_t recycled = Virtual<int64_t>(Pollution(), 0x98);
    float share = 0.0f;
    if (produced != 0)
        share = static_cast<float>(static_cast<double>(recycled) / static_cast<double>(produced));
    float capacity = 0.0f;
    Virtual<uint8_t>(Pollution(), 0xDC, about, &capacity);
    double value;
    if (!(capacity > Global<float>(addr::SC4QueryTool_FloatB21854))) {
        value = static_cast<double>(Global<float>(addr::SC4_FloatZero));
    } else {
        value = static_cast<double>(share) / static_cast<double>(capacity);
        if (value > static_cast<double>(kOne))
            value = static_cast<double>(kOne);
    }
    return SetChildPercent(window, id, Ftol(value * static_cast<double>(kHundred)));
}

// The game's 0x004C8550 (table index 96): with `fill`, last month's
// recycled garbage (slot 38, rounded to a float) times the scaling factor,
// truncated to 64 bits and divided (signed, 0x009EFEB0) by the number of
// recycling centres (slot 44), as text; 0 without a centre.
uint8_t __cdecl FillRecycledPerCenter(void* window, uint32_t id, uint32_t fill, void*)
{
    if (!static_cast<uint8_t>(fill))
        return 1;
    const float recycled = static_cast<float>(Virtual<int64_t>(Pollution(), 0x98));
    const float scale = Virtual<float>(Pollution(), 0xC0);
    const int64_t amount = Ftol64(static_cast<double>(scale) * static_cast<double>(recycled));
    const int32_t centres = Virtual<int32_t>(Pollution(), 0xB0);
    if (!centres)
        return SetChildInt(window, id, 0);
    return SetChildInt(window, id, static_cast<int32_t>(amount / static_cast<int64_t>(centres)));
}

// --- Power plants (the power simulator, 0x00B43DA0) ---------------------------------------

void* Power()
{
    return Global<void*>(addr::SC4_Service43DA0);
}

// The game's 0x004C8660 (table index 53; index 46 jumps here through
// 0x004C9FE0): with `fill`, a plant's two produced numbers (slot 33, both 0
// before) as the second over the first in percent, 32-bit unsigned; 0 when
// the first is 0.
uint8_t __cdecl FillPowerUsedPercent(void* window, uint32_t id, uint32_t fill, void* about)
{
    if (!static_cast<uint8_t>(fill))
        return 1;
    uint32_t produced = 0, used = 0;
    Virtual<uint8_t>(Power(), 0x84, about, &produced, &used);
    const uint32_t percent = produced ? used * 100u / produced : 0;
    return SetChildPercent(window, id, static_cast<int32_t>(percent));
}

// The game's 0x004C86D0 (table indexes 47 and 54): with `fill`, the most a
// plant can produce (slot 34) as text. The game's place is the `fill`
// argument's (not cleared), so a simulator that writes nothing leaves that
// dword. True whatever the text did.
uint8_t __cdecl FillPowerMaxProduced(void* window, uint32_t id, uint32_t fill, void* about)
{
    if (!static_cast<uint8_t>(fill))
        return 1;
    uint32_t produced = fill;
    Virtual<uint8_t>(Power(), 0x88, about, &produced);
    SetChildInt(window, id, static_cast<int32_t>(produced));
    return 1;
}

// The game's 0x004C8710 (table indexes 48 and 55): with `fill`, the first
// of slot 33's numbers (both 0 before) as text.
uint8_t __cdecl FillPowerProduced(void* window, uint32_t id, uint32_t fill, void* about)
{
    if (!static_cast<uint8_t>(fill))
        return 1;
    uint32_t produced = 0, used = 0;
    Virtual<uint8_t>(Power(), 0x84, about, &produced, &used);
    return SetChildInt(window, id, static_cast<int32_t>(produced));
}

// The game's 0x004C8760 (table index 99): a plant's funding on a slider.
// With `fill`: the funding share (slot 50, 0 before) and the monthly cost
// (slot 49, 64 bits, 0 before), false when either says no; the share times
// 100 rounded is the slider's value, the cost its cost, the filler's own
// address its function; what the slider answers. Without it the slider's
// number (unset without the window or the child: 0 here) times 0.01 goes
// to slot 51: the plant's funding changes. True.
uint8_t __cdecl FillPlantFunding(void* window, uint32_t id, uint32_t fill, void* about)
{
    if (static_cast<uint8_t>(fill)) {
        float share = 0.0f;
        if (!Virtual<uint8_t>(Power(), 0xC8, about, &share))
            return 0;
        int64_t cost = 0;
        if (!Virtual<uint8_t>(Power(), 0xC4, about, &cost))
            return 0;
        return SetChildSlider(window, id, Percent(share), cost, about, addr::SC4QueryTool_FillPlantFunding, nullptr,
                              0);
    }
    int32_t number = 0;
    void* control = nullptr;
    if (window && Virtual<uint8_t>(window, 0x94, id, kNumberControl, &control)) {
        number = Virtual<int32_t>(control, 0x10);
        Release(control);
    }
    float share = static_cast<float>(static_cast<double>(number) * static_cast<double>(kHundredth));
    Virtual<uint8_t>(Power(), 0xCC, about, &share);
    return 1;
}

// --- Water buildings (the plumbing simulator, 0x00B43D94) ---------------------------------

// The simulator's slot 24 (GetWaterProducerInfo: the building, then
// production, capacity, a third number, a flag byte and the amount used,
// each written only where a place is given); its answer.
uint8_t ProducerInfo(void* about, uint32_t* production, uint32_t* capacity, uint32_t* used)
{
    return Virtual<uint8_t>(Global<void*>(addr::SC4_Service43D94), 0x60, about, production, capacity,
                            static_cast<uint32_t*>(nullptr), static_cast<uint8_t*>(nullptr), used);
}

// The game's 0x004C8880 (table index 59): with `fill`, the amount used
// over the production in percent, 32-bit unsigned; 0 without production;
// false when the simulator says no. The production's place is the `fill`
// argument's and the amount's is not set (0 here): the game does not clear
// them.
uint8_t __cdecl FillWaterUsedPercent(void* window, uint32_t id, uint32_t fill, void* about)
{
    if (!static_cast<uint8_t>(fill))
        return 1;
    uint32_t production = fill;
    uint32_t used = 0;
    if (!ProducerInfo(about, &production, nullptr, &used))
        return 0;
    if (!production)
        return SetChildPercent(window, id, 0);
    return SetChildPercent(window, id, static_cast<int32_t>(used * 100u / production));
}

// What 0x004C8900, 0x004C8950 and 0x004C89A0 share: with `fill`, one of the
// numbers as text (its place the `fill` argument's); false when the
// simulator says no.
uint8_t ProducerNumber(void* window, uint32_t id, uint32_t fill, void* about, int which)
{
    if (!static_cast<uint8_t>(fill))
        return 1;
    uint32_t number = fill;
    const bool known = ProducerInfo(about, which == 0 ? &number : nullptr, which == 1 ? &number : nullptr,
                                    which == 2 ? &number : nullptr) != 0;
    if (!known)
        return 0;
    return SetChildInt(window, id, static_cast<int32_t>(number));
}

// The game's 0x004C8900 (table index 60): the capacity.
uint8_t __cdecl FillWaterCapacity(void* window, uint32_t id, uint32_t fill, void* about)
{
    return ProducerNumber(window, id, fill, about, 1);
}

// The game's 0x004C8950 (table index 61): the production.
uint8_t __cdecl FillWaterProduction(void* window, uint32_t id, uint32_t fill, void* about)
{
    return ProducerNumber(window, id, fill, about, 0);
}

// The game's 0x004C89A0 (table index 62): the amount used.
uint8_t __cdecl FillWaterUsed(void* window, uint32_t id, uint32_t fill, void* about)
{
    return ProducerNumber(window, id, fill, about, 2);
}

// The game's 0x004C89F0 (table index 115): with `fill`, the building's
// condition (slot 25, a float, 0 before) times 100, truncated, as a
// percentage; 0 when the simulator says no.
uint8_t __cdecl FillWaterCondition(void* window, uint32_t id, uint32_t fill, void* about)
{
    if (!static_cast<uint8_t>(fill))
        return 1;
    float condition = 0.0f;
    if (!Virtual<uint8_t>(Global<void*>(addr::SC4_Service43D94), 0x64, about, &condition))
        return SetChildPercent(window, id, 0);
    return SetChildPercent(window, id, Ftol(static_cast<double>(condition) * static_cast<double>(kHundred)));
}

// --- Transit switches (the traffic simulator, 0x00B43DA8) ---------------------------------

// The record for the cell an occupant stands on: its position (slot 8)
// through the city's slot 95 (PositionToCell), then the traffic
// simulator's slot 35 with the record (only +0x8 cleared before). Its
// answer.
uint8_t TransitData(void* occupant, uint32_t* data)
{
    data[2] = 0;
    float position[3];
    Virtual<void>(occupant, 0x20, static_cast<float*>(position));
    int32_t cellX = 0, cellZ = 0;
    Virtual<void>(Global<void*>(addr::SC4_City), 0x17C, position[0], position[2], &cellX, &cellZ);
    return Virtual<uint8_t>(Global<void*>(addr::SC4_Service43DA8), 0x8C, cellX, cellZ, data);
}

// The game's 0x004C8A60 (table index 66): with `fill`, the record's fourth
// number over its second (both unsigned) in percent, truncated; 0 (a zero
// float) when the simulator says no or the second is 0. The occupant is not
// checked.
uint8_t __cdecl FillTransitPercent(void* window, uint32_t id, uint32_t fill, void* occupant)
{
    if (!static_cast<uint8_t>(fill))
        return 1;
    uint32_t data[transit_record::kSize / 4] = {};
    double value = 0.0;
    if (TransitData(occupant, data) && data[transit_record::kSecond / 4] != 0)
        value = Unsigned(data[transit_record::kFourth / 4]) / Unsigned(data[transit_record::kSecond / 4]);
    return SetChildPercent(window, id, Ftol(value * static_cast<double>(kHundred)));
}

// The game's 0x004C8B30 (table index 67): with `fill`, the record's cost
// (taken unsigned) as money; false when the simulator says no.
uint8_t __cdecl FillTransitCost(void* window, uint32_t id, uint32_t fill, void* occupant)
{
    if (!static_cast<uint8_t>(fill))
        return 1;
    uint32_t data[transit_record::kSize / 4] = {};
    if (!TransitData(occupant, data))
        return 0;
    return SetChildMoney(window, id, static_cast<int64_t>(data[transit_record::kCost / 4]));
}

// The game's 0x004C8BC0 (table index 68): the record's fourth number as
// text.
uint8_t __cdecl FillTransitFourth(void* window, uint32_t id, uint32_t fill, void* occupant)
{
    if (!static_cast<uint8_t>(fill))
        return 1;
    uint32_t data[transit_record::kSize / 4] = {};
    if (!TransitData(occupant, data))
        return 0;
    return SetChildInt(window, id, static_cast<int32_t>(data[transit_record::kFourth / 4]));
}

// The game's 0x004C8C50 (table index 69): the record's second number as
// text.
uint8_t __cdecl FillTransitSecond(void* window, uint32_t id, uint32_t fill, void* occupant)
{
    if (!static_cast<uint8_t>(fill))
        return 1;
    uint32_t data[transit_record::kSize / 4] = {};
    if (!TransitData(occupant, data))
        return 0;
    return SetChildInt(window, id, static_cast<int32_t>(data[transit_record::kSecond / 4]));
}

// --- Budget items (the budget simulator, 0x00B43D64) --------------------------------------

// cISC4BudgetSimulator::BudgetItem as the game's code has it.
struct BudgetItem {
    uint32_t department;
    uint32_t lineItem;
    uint32_t purpose;
    uint32_t padding;
    int64_t cost;
    uint32_t index;
    float localFunding; // the one field the callers set (0) before the call
};
static_assert(sizeof(BudgetItem) == 0x20);

void* Budget()
{
    return Global<void*>(addr::SC4_BudgetSimulator);
}

// A department's funding (its slot 10: the float at +0x34).
float DepartmentFunding(void* department)
{
    return *Virtual<float*>(department, 0x28);
}

// The game's 0x004C8CE0 (table index 70): with `fill`, the funding of the
// department 0xC921CEEB (slot 28) times 100, truncated, as a percentage; 0
// without the department.
uint8_t __cdecl FillTransportFunding(void* window, uint32_t id, uint32_t fill, void*)
{
    if (!static_cast<uint8_t>(fill))
        return 1;
    void* const department = Virtual<void*>(Budget(), 0x70, kTransportDepartment);
    if (!department)
        return SetChildPercent(window, id, 0);
    return SetChildPercent(window, id,
                           Ftol(static_cast<double>(DepartmentFunding(department)) * static_cast<double>(kHundred)));
}

// The occupant's budget item for the purpose 0xAA59670C (slot 51 with its
// property holder); the answer is not looked at.
void ItemOf(void* occupant, BudgetItem* item)
{
    void* const budget = Budget();
    Virtual<uint8_t>(budget, 0xCC, Virtual<void*>(occupant, 0x18), kBudgetPurpose, item);
}

// The game's 0x004C8D40 (table index 71): with `fill`, the item's cost as
// money, whether or not the simulator found an item (then the game shows
// what was on the stack; 0 here).
uint8_t __cdecl FillBudgetItemCost(void* window, uint32_t id, uint32_t fill, void* occupant)
{
    if (!static_cast<uint8_t>(fill))
        return 1;
    BudgetItem item = {};
    ItemOf(occupant, &item);
    return SetChildMoney(window, id, item.cost);
}

// The game's 0x004C8DB0 (table index 73): with `fill`, the item's cost
// times its department's funding (slot 28 with the item's department; not
// checked for null), truncated to 64 bits, as money.
uint8_t __cdecl FillBudgetItemFundedCost(void* window, uint32_t id, uint32_t fill, void* occupant)
{
    if (!static_cast<uint8_t>(fill))
        return 1;
    BudgetItem item = {};
    ItemOf(occupant, &item);
    void* const department = Virtual<void*>(Budget(), 0x70, item.department);
    const float funding = DepartmentFunding(department);
    return SetChildMoney(window, id, Ftol64(static_cast<double>(item.cost) * static_cast<double>(funding)));
}

// --- Registration ----------------------------------------------------------------------

bool InstallQuery3a(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[40] = {};
    static const struct {
        std::uintptr_t address;
        void* ours;
    } hooks[] = {
        {addr::SC4QueryTool_FillSchoolFunding, reinterpret_cast<void*>(&FillSchoolFunding)},
        {addr::SC4QueryTool_FillSchoolFundingSecond, reinterpret_cast<void*>(&FillSchoolFundingSecond)},
        {addr::SC4QueryTool_FillCapacityFirst, reinterpret_cast<void*>(&FillCapacityFirst)},
        {addr::SC4QueryTool_FillSchoolNumber, reinterpret_cast<void*>(&FillSchoolNumber)},
        {addr::SC4QueryTool_FillHospitalValue, reinterpret_cast<void*>(&FillHospitalValue)},
        {addr::SC4QueryTool_FillHospitalFunding, reinterpret_cast<void*>(&FillHospitalFunding)},
        {addr::SC4QueryTool_FillHospitalFundingSecond, reinterpret_cast<void*>(&FillHospitalFundingSecond)},
        {addr::SC4QueryTool_FillCapacitySecond, reinterpret_cast<void*>(&FillCapacitySecond)},
        {addr::SC4QueryTool_FillHospitalNumber, reinterpret_cast<void*>(&FillHospitalNumber)},
        {addr::SC4QueryTool_FillGarbageLandfilled, reinterpret_cast<void*>(&FillGarbageLandfilled)},
        {addr::SC4QueryTool_FillLandfillCost, reinterpret_cast<void*>(&FillLandfillCost)},
        {addr::SC4QueryTool_FillIncineratorPercent, reinterpret_cast<void*>(&FillIncineratorPercent)},
        {addr::SC4QueryTool_FillIncineratorUsed, reinterpret_cast<void*>(&FillIncineratorUsed)},
        {addr::SC4QueryTool_FillPropertyFirst, reinterpret_cast<void*>(&FillPropertyFirst)},
        {addr::SC4QueryTool_FillRecycledShare, reinterpret_cast<void*>(&FillRecycledShare)},
        {addr::SC4QueryTool_FillRecycledPerCenter, reinterpret_cast<void*>(&FillRecycledPerCenter)},
        {addr::SC4QueryTool_FillWasteToEnergyUsed, reinterpret_cast<void*>(&FillWasteToEnergyUsed)},
        {addr::SC4QueryTool_FillPowerUsedPercent, reinterpret_cast<void*>(&FillPowerUsedPercent)},
        {addr::SC4QueryTool_FillPowerMaxProduced, reinterpret_cast<void*>(&FillPowerMaxProduced)},
        {addr::SC4QueryTool_FillPowerProduced, reinterpret_cast<void*>(&FillPowerProduced)},
        {addr::SC4QueryTool_FillPlantFunding, reinterpret_cast<void*>(&FillPlantFunding)},
        {addr::SC4QueryTool_FillWaterUsedPercent, reinterpret_cast<void*>(&FillWaterUsedPercent)},
        {addr::SC4QueryTool_FillWaterCapacity, reinterpret_cast<void*>(&FillWaterCapacity)},
        {addr::SC4QueryTool_FillWaterProduction, reinterpret_cast<void*>(&FillWaterProduction)},
        {addr::SC4QueryTool_FillWaterUsed, reinterpret_cast<void*>(&FillWaterUsed)},
        {addr::SC4QueryTool_FillWaterCondition, reinterpret_cast<void*>(&FillWaterCondition)},
        {addr::SC4QueryTool_FillTransitPercent, reinterpret_cast<void*>(&FillTransitPercent)},
        {addr::SC4QueryTool_FillTransitCost, reinterpret_cast<void*>(&FillTransitCost)},
        {addr::SC4QueryTool_FillTransitFourth, reinterpret_cast<void*>(&FillTransitFourth)},
        {addr::SC4QueryTool_FillTransitSecond, reinterpret_cast<void*>(&FillTransitSecond)},
        {addr::SC4QueryTool_FillTransportFunding, reinterpret_cast<void*>(&FillTransportFunding)},
        {addr::SC4QueryTool_FillBudgetItemCost, reinterpret_cast<void*>(&FillBudgetItemCost)},
        {addr::SC4QueryTool_FillBudgetItemFundedCost, reinterpret_cast<void*>(&FillBudgetItemFundedCost)},
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
    "ui.tools.query3a",
    "ui",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4QueryTool_FillSchoolFunding,
    "Our query tool, third piece, first half: the fillers of schools, garbage, power, water, transit and budget items "
    "(docs/tools.md)",
    false,
    InstallQuery3a,
});

} // namespace
} // namespace opensc4::tool_query_3a
