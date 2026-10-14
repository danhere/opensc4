// The query tool of the city view, first piece, first half: the plain
// functions between 0x004C3750 and 0x004C5617 that the tool's windows are
// filled with.
// Replacement, off by default:
//
//   ui.tools.query1a: 34 cdecl functions of the query tool
//     (cSC4ViewInputControlQuery, docs/tools.md): the log line, the helpers
//     that set a control of a window, twelve entries of the filler table at
//     0x00A92488, the measures of a lot the fillers and the texts use, the
//     two walks over a window's children and the five map levels.
//
// A control of a query window whose ID has the bit 0x1000 is filled by the
// table's function at the ID's low 12 bits: cdecl (window, control ID, fill,
// what is asked about). With `fill` the function works its value out and
// stores it in the control; without it the function is to read the control
// back, which only the historical check box does (0x004C55A0: it sets the
// lot's flag, the one function here that changes the city). A value is a
// float kept as the control's property 0xEA563AB0 (0x004C3CA0).
//
// Entries of the table that are functions of this file (index, address):
// 1 0x004C5180, 6 0x004C51C0, 12 0x004C5200, 21 0x004C5310, 41 0x004C3E70,
// 45 0x004C5350, 52 0x004C3F90, 58 and 109 0x004C42F0, 65 0x004C3FD0, 94
// 0x004C3EE0, 122 0x004C55A0. The plugin sc4-advanced-water-management
// changes the table's word at 0x00A92570 (index 58): the functions are
// hooked at their entries, never by the table, so the plugin's function is
// called where it put it and ours at index 109.
//
// Nothing here reads a clock or draws a random number. The log line goes to
// the object at 0x00B21850, which is only there while a query's debug
// window is.
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstdarg>
#include <cstddef>
#include <cstdint>

namespace opensc4::tool_query_1a {
namespace {

constexpr uint32_t kValueControl = 0xC12CEA13; // the interface of a control that shows a value
constexpr uint32_t kValueProperty = 0xEA563AB0; // the window property the value is kept as
constexpr uint32_t kTextEdit = 0x231A1C57;
constexpr uint32_t kTextControl = 0x212CDC1F;
constexpr uint32_t kNumberControl = 0x21325207;
constexpr uint32_t kChildren = 0x22BA0121;      // what a window's slot 32 walks
constexpr uint32_t kBuildingPart = 0x87DDA3A9;  // an occupant's interface with the capacity record

// The floats at 0x00A9095C and 0x00A90A68 (0x3F4CCCCD, 0x3E4CCCCC: the
// second is one bit under 0.2f), 0x00A90A74 and 0x00A90A70, 0x00A90B34
// (0x3EC8C8C9, 100 / 255).
constexpr float kShareFree = 0.8f;
constexpr float kShareRest = 0x1.999998p-3f;
constexpr float kLevelLow = 33.3f;
constexpr float kLevelHigh = 66.6f;
constexpr float kPercentOfByte = 0x1.919192p-2f;

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

uint32_t Bits(float value)
{
    return At<uint32_t>(&value, 0);
}

// __ftol2: truncation toward zero, the low 32 bits.
int32_t Ftol(double value)
{
    return static_cast<int32_t>(static_cast<int64_t>(value));
}

// The cell an occupant stands on: its position (slot 8) through the city's
// slot 95.
void CellOf(void* occupant, int32_t* cellX, int32_t* cellZ)
{
    float position[3];
    Virtual<void>(occupant, 0x20, static_cast<float*>(position));
    Virtual<void>(Global<void*>(addr::SC4_City), 0x17C, position[0], position[2], cellX, cellZ);
}

// --- The log line and a window's controls ----------------------------------------------

// The game's 0x004C3750 (cdecl, variadic; 207 calls in the class): a log
// line. The text is formatted into 0x200 bytes on the stack (the C
// runtime's _vsnprintf with 0x1FF, the last byte cleared) and given to the
// object at 0x00B21850 (slot 14) when there is one.
void __cdecl Log(const char* format, ...)
{
    char text[0x200];
    va_list arguments;
    va_start(arguments, format);
    Cdecl<int32_t>(addr::CRT_vsnprintf, static_cast<char*>(text), uint32_t{0x1FF}, format, arguments);
    va_end(arguments);
    text[0x1FF] = 0;
    if (void* const sink = Global<void*>(addr::SC4QueryTool_LogSink))
        Virtual<void>(sink, 0x38, static_cast<char*>(text));
}

// The game's 0x004C3B10 (cdecl, the result on the x87 stack): what is left
// of a capacity as a share. 0 (the float at 0x00A81054) without a capacity;
// 1 while no more than 0.8 of it is used; else what is not used over 0.2 of
// the capacity (0 when more is used than there is). Both are unsigned.
double __cdecl Share(uint32_t used, uint32_t capacity)
{
    if (capacity == 0)
        return static_cast<double>(Global<float>(addr::SC4_FloatZero));
    const double whole = static_cast<double>(capacity);
    if (whole * static_cast<double>(kShareFree) >= static_cast<double>(used))
        return 1.0;
    const uint32_t left = used < capacity ? capacity - used : 0;
    return static_cast<double>(left) / (whole * static_cast<double>(kShareRest));
}

// The game's 0x004C3B90 (cdecl): the text of a text edit control. The
// window's child by ID as the interface 0x231A1C57 (the window's slot 37):
// its slot 48 with the text and 0, slot 78 with 8, slot 76 with 0x200; let
// go. False without the window or the child.
uint8_t __cdecl SetChildEdit(void* window, uint32_t id, void* text)
{
    void* control = nullptr;
    if (!window || !Virtual<uint8_t>(window, 0x94, id, kTextEdit, &control))
        return 0;
    Virtual<void>(control, 0xC0, text, uint32_t{0});
    Virtual<void>(control, 0x138, uint32_t{8});
    Virtual<void>(control, 0x130, uint32_t{0x200});
    Release(control);
    return 1;
}

// The game's 0x004C3C00 (cdecl; 16 calls): the caption of a text control
// (interface 0x212CDC1F, its slot 4). False without the window or the
// child.
uint8_t __cdecl SetChildText(void* window, uint32_t id, void* text)
{
    void* control = nullptr;
    if (!window || !Virtual<uint8_t>(window, 0x94, id, kTextControl, &control))
        return 0;
    Virtual<void>(control, 0x10, text);
    Release(control);
    return 1;
}

// The game's 0x004C3C50 (cdecl): the number of a control (interface
// 0x21325207, its slot 4) into *out. False, and nothing written, without
// the window or the child.
uint8_t __cdecl GetChildNumber(void* window, uint32_t id, uint32_t* out)
{
    void* control = nullptr;
    if (!window || !Virtual<uint8_t>(window, 0x94, id, kNumberControl, &control))
        return 0;
    *out = Virtual<uint32_t>(control, 0x10);
    Release(control);
    return 1;
}

// The game's 0x004C3CA0 (cdecl; the value is a float, passed on as its four
// bytes): the value of a control. The child by ID as the interface
// 0xC12CEA13; its window (slot 3) is asked for the property 0xEA563AB0
// (slot 113). When it has none, a script value of 0x14 bytes is made
// (0x009166D4) and referenced; when it has one, the reference the answer
// came with is given back at once and the object used all the same. The
// value is set (the object's slot 38) and the object made the window's
// property (slot 114). A value made here keeps the reference it was given.
// False without the window or the child. A failed allocation calls through
// null, as in the game.
uint8_t __cdecl SetChildValue(void* window, uint32_t id, uint32_t value)
{
    void* control = nullptr;
    if (!window || !Virtual<uint8_t>(window, 0x94, id, kValueControl, &control))
        return 0;
    void* number = nullptr;
    if (!Virtual<uint8_t>(Virtual<void*>(control, 0xC), 0x1C4, kValueProperty, &number)) {
        void* const memory = Cdecl<void*>(addr::SC4_operator_new, uint32_t{0x14});
        number = memory ? This<void*>(addr::SC4ScriptNumber_Construct, memory) : nullptr;
        AddRef(number);
    } else {
        Release(number);
    }
    Virtual<void>(number, 0x98, value);
    Virtual<void>(Virtual<void*>(control, 0xC), 0x1C8, kValueProperty, number);
    Release(control);
    return 1;
}

// --- The walks over a window's children -------------------------------------------------

// The game's 0x004C4340 (cdecl; what a window's slot 32 calls for each
// child: the child, its ID, the child as the interface 0x22BA0121, the
// walk's own value): the child's slot 116 with the value, then the same
// walk over the child's children. True. The walk is given the game's
// address of this function, as the game gives it.
uint8_t __cdecl AttachChildren(void*, uint32_t, void* child, void* value)
{
    Virtual<void>(child, 0x1D0, value);
    Virtual<void>(child, 0x80, kChildren, std::uintptr_t{addr::SC4QueryTool_AttachChildren}, value);
    return 1;
}

// The game's 0x004C4370 (cdecl, the same four arguments): the child's slot
// 117 with the value; then each of five properties the child has (slot 113)
// is taken from it (slot 114 with null) and let go twice: 0xEA563AB0 (the
// value of 0x004C3CA0), 0x8ADC29D4, 0x6A8CC9D6, 0x2AA6E8AD, 0x0AB3F6DB; then
// the same walk over the child's children. True.
uint8_t __cdecl DetachChildren(void*, uint32_t, void* child, void* value)
{
    static const uint32_t properties[] = {kValueProperty, 0x8ADC29D4, 0x6A8CC9D6, 0x2AA6E8AD, 0x0AB3F6DB};
    Virtual<void>(child, 0x1D4, value);
    for (const uint32_t property : properties) {
        void* held = nullptr;
        if (Virtual<uint8_t>(child, 0x1C4, property, &held)) {
            Virtual<void>(child, 0x1C8, property, static_cast<void*>(nullptr));
            Release(held);
            Release(held);
        }
    }
    Virtual<void>(child, 0x80, kChildren, std::uintptr_t{addr::SC4QueryTool_DetachChildren}, value);
    return 1;
}

// --- Measures of an occupant's lot ------------------------------------------------------

// The game's 0x004C40B0 (cdecl; ECX and EDX are not touched): a number of 1
// to 12 to a kind: 2, 3, 8, 10 and 12 give 3, 4 gives 1, 5 gives 2, every
// other -1.
int32_t __cdecl KindOfNumber(int32_t number)
{
    switch (number) {
    case 2:
    case 3:
    case 8:
    case 10:
    case 12:
        return 3;
    case 4:
        return 1;
    case 5:
        return 2;
    default:
        return -1;
    }
}

// The game's 0x004C4100 (cdecl): the two occupant types of a kind: 1
// 0x4773FFE0 and 0x4773FFE1, 2 0x4773FFE2 and 0x4773FFE3, 3 0x4773FFE4 and
// 0x4773FFE5. False, and nothing written, for another kind.
uint8_t __cdecl TypesOfKind(int32_t kind, uint32_t* first, uint32_t* second)
{
    if (kind < 1 || kind > 3)
        return 0;
    *first = 0x4773FFE0 + 2 * static_cast<uint32_t>(kind - 1);
    *second = 0x4773FFE1 + 2 * static_cast<uint32_t>(kind - 1);
    return 1;
}

// The game's 0x004C4160 (cdecl): the capacity of the lot an occupant stands
// on: the lot at its cell (the lot manager's slot 6 with 0), the lot's
// capacity (slot 51, 16 bits) for its occupants' wealth (slot 41). When
// that is 0 the occupant is asked for the interface 0x87DDA3A9, and the
// answer is the 16 bits at +8 of what its slot 11 gives. 0 without a lot.
uint32_t __cdecl LotCapacity(void* occupant)
{
    int32_t cellX, cellZ;
    CellOf(occupant, &cellX, &cellZ);
    void* const lot = Virtual<void*>(Global<void*>(addr::SC4_LotManager), 0x18, cellX, cellZ, uint32_t{0});
    if (!lot)
        return 0;
    const uint32_t wealth = Virtual<uint32_t>(lot, 0xA4);
    uint32_t capacity = Virtual<uint16_t>(lot, 0xCC, wealth);
    if (capacity != 0)
        return capacity;
    void* part = nullptr;
    if (Virtual<uint8_t>(occupant, 0x0, kBuildingPart, &part)) {
        capacity = At<uint16_t>(Virtual<char*>(part, 0x2C), 0x8);
        Release(part);
    }
    return capacity;
}

// The game's 0x004C4210 (cdecl): what the service at 0x00B43D88 has (slot 5,
// its whole EAX) at the cell an occupant stands on. The one caller takes 1,
// 2 and 3 apart.
uint32_t __cdecl CellGradeOf(void* occupant)
{
    int32_t cellX, cellZ;
    CellOf(occupant, &cellX, &cellZ);
    return Virtual<uint32_t>(Global<void*>(addr::SC4_Service43D88), 0x14, cellX, cellZ);
}

// The game's 0x004C4260 (cdecl): the text for a level into a string: below
// 33.3 the text 0x4A5FE266 (Low), below 66.6 0x0A5FE260 (Medium), from there
// on (and for what is not a number) 0xAA5FE249 (High), all of the group
// 0xEA5524EB. What the loading returns.
uint32_t __cdecl LevelText(void* text, float level)
{
    uint32_t instance = 0xAA5FE249;
    if (level < kLevelLow)
        instance = 0x4A5FE266;
    else if (level < kLevelHigh)
        instance = 0x0A5FE260;
    return Cdecl<uint32_t>(addr::SC4_LoadLocalizedText, text, uint32_t{0xEA5524EB}, instance, uint32_t{0});
}

// The game's 0x004C53A0 (cdecl): how many are on the lot an occupant stands
// on: the lot's capacity (0x004C4160) times its occupancy (the lot's slot
// 61), truncated. 0 without a lot or without a building type (slot 46 with
// 1). On the way the development service (0x00B43D68) is asked for the
// building type's profile (slot 39) and with the profile's first word and
// the lot's occupant wealth for its slot 30, whose answer is dropped.
uint32_t __cdecl LotPopulation(void* occupant)
{
    int32_t cellX, cellZ;
    CellOf(occupant, &cellX, &cellZ);
    void* const lot = Virtual<void*>(Global<void*>(addr::SC4_LotManager), 0x18, cellX, cellZ, uint32_t{0});
    if (!lot)
        return 0;
    const uint32_t wealth = Virtual<uint32_t>(lot, 0xA4);
    const uint32_t building = Virtual<uint32_t>(lot, 0xB8, uint32_t{1});
    if (building == 0)
        return 0;
    void* const development = Global<void*>(addr::SC4_Service43D68);
    const uint32_t purpose = *Virtual<uint32_t*>(development, 0x9C, building);
    Virtual<uint32_t>(development, 0x78, purpose, wealth);
    float room[4];
    const float occupancy = *Virtual<float*>(lot, 0xF4, static_cast<float*>(room));
    const uint32_t capacity = LotCapacity(occupant);
    return static_cast<uint32_t>(Ftol(static_cast<double>(capacity) * static_cast<double>(occupancy)));
}

// The game's 0x004C5490 (cdecl, the result on the x87 stack): the average
// over the cells of the lot an occupant stands on (the lot's slot 14 gives
// the cells' box) of the signed byte the service at 0x00B43D60 has for a
// cell (slot 5) plus 0x80, times 100 / 255. 0 without a lot. A box without
// cells divides 0 by 0, as in the game.
double __cdecl LotAverage(void* occupant)
{
    const float none = 0.0f;
    int32_t cellX, cellZ;
    CellOf(occupant, &cellX, &cellZ);
    void* const lot = Virtual<void*>(Global<void*>(addr::SC4_LotManager), 0x18, cellX, cellZ, uint32_t{0});
    if (!lot)
        return static_cast<double>(none);
    int32_t box[4];
    Virtual<void>(lot, 0x38, static_cast<int32_t*>(box));
    uint32_t sum = 0;
    uint32_t count = 0;
    for (int32_t x = box[0]; x <= box[2]; ++x) {
        if (box[1] > box[3])
            continue;
        count += static_cast<uint32_t>(box[3] - box[1] + 1);
        for (int32_t z = box[1]; z <= box[3]; ++z) {
            const int8_t value =
                static_cast<int8_t>(Virtual<uint8_t>(Global<void*>(addr::SC4_Service43D60), 0x14, x, z));
            sum += static_cast<uint32_t>(static_cast<int32_t>(value) + 0x80);
        }
    }
    return static_cast<double>(sum) / static_cast<double>(count) * static_cast<double>(kPercentOfByte);
}

// --- The fillers ------------------------------------------------------------------------

// What 0x004C3D70 and 0x004C5280 do: the share left (0x004C3B10) of what
// the city's object at `slot` says of the lot at a cell (its slot 12: the
// lot, two numbers out, the used one first), as a control's value; 0
// without a lot or when the object says no. The game leaves the two numbers
// as they come before the call.
uint8_t FillLotShare(void* window, uint32_t id, int32_t x, int32_t z, std::size_t slot)
{
    float value = 0.0f;
    if (void* const lot = Virtual<void*>(Global<void*>(addr::SC4_LotManager), 0x18, x, z, uint32_t{0})) {
        void* const owner = Virtual<void*>(Global<void*>(addr::SC4_City), slot);
        uint32_t used = 0;
        uint32_t capacity = 0;
        if (Virtual<uint8_t>(owner, 0x30, lot, &used, &capacity))
            value = static_cast<float>(Share(used, capacity));
    }
    return SetChildValue(window, id, Bits(value));
}

// The game's 0x004C3D70 (cdecl): with `fill`, the share left of the lot at
// a cell by the city's slot 76. True without `fill`.
uint8_t __cdecl FillLotShareFirst(void* window, uint32_t id, uint32_t fill, int32_t x, int32_t z)
{
    if (!static_cast<uint8_t>(fill))
        return 1;
    return FillLotShare(window, id, x, z, 0x130);
}

// The game's 0x004C5280 (cdecl): the same by the city's slot 77.
uint8_t __cdecl FillLotShareSecond(void* window, uint32_t id, uint32_t fill, int32_t x, int32_t z)
{
    if (!static_cast<uint8_t>(fill))
        return 1;
    return FillLotShare(window, id, x, z, 0x134);
}

// The game's 0x004C3E10 (cdecl; its caller pushes two more arguments, which
// are not read): with `fill`, the share left of the two numbers the object
// at 0x00B43D28 gives (slot 10; both 0 before the call).
uint8_t __cdecl FillCityShare(void* window, uint32_t id, uint32_t fill)
{
    if (!static_cast<uint8_t>(fill))
        return 1;
    uint32_t used = 0;
    uint32_t capacity = 0;
    Virtual<void>(Global<void*>(addr::SC4_Value43D28), 0x28, &used, &capacity);
    return SetChildValue(window, id, Bits(static_cast<float>(Share(used, capacity))));
}

// The game's 0x004C5310 (table index 21): the share of 0x004C3D70 at the
// first cell of the box an occupant gives (its slot 11, 16 bytes). False
// without the occupant, which is looked at before `fill`.
uint8_t __cdecl FillOccupantLotShare(void* window, uint32_t id, uint32_t fill, void* occupant)
{
    if (!occupant)
        return 0;
    int32_t box[4];
    Virtual<void>(occupant, 0x2C, static_cast<int32_t*>(box));
    return FillLotShareFirst(window, id, fill, box[0], box[1]);
}

// Four fillers alike: a service's slot gives a float through a place of the
// caller's (the place, what is asked about; the answer is where the float
// is), and its four bytes are the control's value.
uint8_t FillFloatOf(void* window, uint32_t id, uint32_t fill, void* about, std::uintptr_t service, std::size_t slot)
{
    if (!static_cast<uint8_t>(fill))
        return 1;
    uint32_t place = 0;
    const uint32_t value = *Virtual<uint32_t*>(Global<void*>(service), slot, &place, about);
    return SetChildValue(window, id, value);
}

// The game's 0x004C3F90 (table index 52): the service at 0x00B43DA0, slot
// 45.
uint8_t __cdecl FillFloatDA0(void* window, uint32_t id, uint32_t fill, void* about)
{
    return FillFloatOf(window, id, fill, about, addr::SC4_Service43DA0, 0xB4);
}

// The game's 0x004C5350 (table index 45): the same service and slot, the
// place another stack slot.
uint8_t __cdecl FillFloatDA0Other(void* window, uint32_t id, uint32_t fill, void* about)
{
    return FillFloatOf(window, id, fill, about, addr::SC4_Service43DA0, 0xB4);
}

// The game's 0x004C5180 (table index 1): the service at 0x00B43D78, slot 10.
uint8_t __cdecl FillFloatD78(void* window, uint32_t id, uint32_t fill, void* about)
{
    return FillFloatOf(window, id, fill, about, addr::SC4_Service43D78, 0x28);
}

// The game's 0x004C51C0 (table index 6): the police simulator, slot 25.
uint8_t __cdecl FillFloatPolice(void* window, uint32_t id, uint32_t fill, void* about)
{
    return FillFloatOf(window, id, fill, about, addr::SC4_PoliceSimulator, 0x64);
}

// The game's 0x004C5200 (table index 12): the police simulator's float
// (slot 36, through a place) times its number for what is asked about (slot
// 28, unsigned), truncated, is what is used of that number; the share left.
uint8_t __cdecl FillPoliceShare(void* window, uint32_t id, uint32_t fill, void* about)
{
    if (!static_cast<uint8_t>(fill))
        return 1;
    uint32_t place = 0;
    const float factor = *Virtual<float*>(Global<void*>(addr::SC4_PoliceSimulator), 0x90, &place);
    const uint32_t number = Virtual<uint32_t>(Global<void*>(addr::SC4_PoliceSimulator), 0x70, about);
    const uint32_t used = static_cast<uint32_t>(Ftol(static_cast<double>(number) * static_cast<double>(factor)));
    return SetChildValue(window, id, Bits(static_cast<float>(Share(used, number))));
}

// The game's 0x004C3E70 (table index 41): the pollution simulator's float
// for what is asked about (slot 57; 0 before the call) times 100,
// truncated, as an unsigned number.
uint8_t __cdecl FillPollutionPercent(void* window, uint32_t id, uint32_t fill, void* about)
{
    if (!static_cast<uint8_t>(fill))
        return 1;
    float value = 0.0f;
    Virtual<void>(Global<void*>(addr::SC4_PollutionSimulator), 0xE4, about, &value);
    const uint32_t percent = static_cast<uint32_t>(Ftol(static_cast<double>(value) * 100.0));
    return SetChildValue(window, id, Bits(static_cast<float>(percent)));
}

// The game's 0x004C3EE0 (table index 94): two floats of the pollution
// simulator for what is asked about (slots 54 and 55; both 0 before): when
// the second is above the float at 0x00B21854, the second over the first,
// at most 1; else 0.
uint8_t __cdecl FillPollutionShare(void* window, uint32_t id, uint32_t fill, void* about)
{
    if (!static_cast<uint8_t>(fill))
        return 1;
    float first = 0.0f;
    float second = 0.0f;
    Virtual<void>(Global<void*>(addr::SC4_PollutionSimulator), 0xD8, about, &first);
    Virtual<void>(Global<void*>(addr::SC4_PollutionSimulator), 0xDC, about, &second);
    float value = 0.0f;
    if (second > Global<float>(addr::SC4QueryTool_FloatB21854)) {
        const double share = static_cast<double>(second) / static_cast<double>(first);
        value = static_cast<float>(share);
        if (share > 1.0)
            value = 1.0f;
    }
    return SetChildValue(window, id, Bits(value));
}

// The game's 0x004C3FD0 (table index 65): the service at 0x00B43DA8 fills
// five dwords for the cell an occupant stands on (slot 35; the third is 0
// before the call). When it says yes and the first is not 0: 1 when the
// fourth is 0, else the first over the fourth (both unsigned), at most 1.
// Else 0.
uint8_t __cdecl FillCellShare(void* window, uint32_t id, uint32_t fill, void* occupant)
{
    if (!static_cast<uint8_t>(fill))
        return 1;
    uint32_t record[5];
    record[2] = 0;
    int32_t cellX, cellZ;
    CellOf(occupant, &cellX, &cellZ);
    float value = 0.0f;
    if (Virtual<uint8_t>(Global<void*>(addr::SC4_Service43DA8), 0x8C, cellX, cellZ, static_cast<uint32_t*>(record)) &&
        record[0] != 0) {
        if (record[3] == 0) {
            value = 1.0f;
        } else {
            const double share = static_cast<double>(record[0]) / static_cast<double>(record[3]);
            value = static_cast<float>(share);
            if (share > 1.0)
                value = 1.0f;
        }
    }
    return SetChildValue(window, id, Bits(value));
}

// The game's 0x004C42F0 (table indexes 58 and 109): the plumbing
// simulator's float for what is asked about (0x00B43D94, slot 24: it, 0, 0,
// the float out, 0, 0; the float 0 before). False when the simulator says
// no.
uint8_t __cdecl FillWater(void* window, uint32_t id, uint32_t fill, void* about)
{
    if (!static_cast<uint8_t>(fill))
        return 1;
    uint32_t value = 0;
    if (!Virtual<uint8_t>(Global<void*>(addr::SC4_Service43D94), 0x60, about, uint32_t{0}, uint32_t{0}, &value,
                          uint32_t{0}, uint32_t{0}))
        return 0;
    return SetChildValue(window, id, value);
}

// The game's 0x004C55A0 (table index 122): the historical check box. The
// lot of an occupant (the lot manager's slot 14), held for the call: with
// `fill` the box is checked as the lot is historical (the lot's slot 43);
// without it the lot is made historical or not as the box is checked (slot
// 44): the one function of this piece that changes the city. False without
// a lot.
uint8_t __cdecl FillHistorical(void* window, uint32_t id, uint32_t fill, void* occupant)
{
    void* const lot = Virtual<void*>(Global<void*>(addr::SC4_LotManager), 0x38, occupant);
    if (!lot)
        return 0;
    AddRef(lot);
    if (static_cast<uint8_t>(fill))
        Cdecl<uint8_t>(addr::SC4Win_CheckChild, window, id, Virtual<uint32_t>(lot, 0xAC));
    else
        Virtual<void>(lot, 0xB0, Cdecl<uint32_t>(addr::SC4Win_IsChildChecked, window, id));
    Release(lot);
    return 1;
}

// --- The map levels ---------------------------------------------------------------------

// Five functions alike (cdecl: an object, a cell, a number out; only caller
// 0x004D4FD0, which has the object from 0x004D32D0 for a lot: by its curves
// a growth developer, docs/growth.md; not checked): the object's slot 30
// gives two bounds for a kind of 2 to 6; a grid's value at the cell goes
// through one of the object's curves (+0x74 to +0x84) and is truncated.
// Below the lower bound the number out is the value negated, from the upper
// bound on it is the value halved (towards zero), between them 0 with the
// answer 1.
uint32_t Band(int32_t value, int32_t low, int32_t high, int32_t* out, uint32_t below, uint32_t above)
{
    if (value < low) {
        *out = -value;
        return below;
    }
    if (value >= high) {
        *out = value / 2;
        return above;
    }
    *out = 0;
    return 1;
}

int32_t Curved(char* maps, std::size_t curve, float value)
{
    return Ftol(This<double>(addr::SC4Curve_Evaluate, At<void*>(maps, curve), value));
}

// The game's 0x004C4750: kind 2, the curve at +0x74, the float grid from
// the service at 0x00B43DA4 (slot 39; the grid's slot 7): 0 below, 2 above.
// Without the curve the number out is 0 and the answer 1.
uint32_t __cdecl LevelFirst(char* maps, int32_t x, int32_t z, int32_t* out)
{
    int32_t low = 0, high = 0;
    Virtual<void>(maps, 0x78, uint32_t{2}, &low, &high);
    if (!At<void*>(maps, 0x74)) {
        *out = 0;
        return 1;
    }
    void* const grid = Virtual<void*>(Global<void*>(addr::SC4_Service43DA4), 0x9C);
    const float value = Virtual<float>(grid, 0x1C, x, z);
    return Band(Curved(maps, 0x74, value), low, high, out, 0, 2);
}

// The game's 0x004C47F0: kind 3, the curve at +0x78, the float grid of the
// same service's slot 40: 0 below, 2 above.
uint32_t __cdecl LevelSecond(char* maps, int32_t x, int32_t z, int32_t* out)
{
    int32_t low = 0, high = 0;
    Virtual<void>(maps, 0x78, uint32_t{3}, &low, &high);
    if (!At<void*>(maps, 0x78)) {
        *out = 0;
        return 1;
    }
    void* const grid = Virtual<void*>(Global<void*>(addr::SC4_Service43DA4), 0xA0);
    const float value = Virtual<float>(grid, 0x1C, x, z);
    return Band(Curved(maps, 0x78, value), low, high, out, 0, 2);
}

// The game's 0x004C4890: kind 4, the curve at +0x7C, the byte grid of the
// service at 0x00B43D70 (slot 5): 2 below, 0 above.
uint32_t __cdecl LevelThird(char* maps, int32_t x, int32_t z, int32_t* out)
{
    int32_t low = 0, high = 0;
    Virtual<void>(maps, 0x78, uint32_t{4}, &low, &high);
    if (!At<void*>(maps, 0x7C)) {
        *out = 0;
        return 1;
    }
    void* const grid = Virtual<void*>(Global<void*>(addr::SC4_Service43D70), 0x14);
    const uint32_t value = Virtual<uint8_t>(grid, 0x1C, x, z);
    return Band(Curved(maps, 0x7C, static_cast<float>(value)), low, high, out, 2, 0);
}

// The game's 0x004C4930: kind 5, the curve at +0x84, the byte grid of the
// service at 0x00B43DA8 (slot 9). The curve is taken at the cell's byte
// (kept as a float) and at the byte plus 1. With the lower bound above the
// first: the number out is the first negated and truncated, the answer 2
// when the first is above the second, else 0. Else with the upper bound
// above the first (or the first not a number): 1, nothing written. Else the
// number out is half the first, truncated, the answer 0 when the first is
// above the second, else 2. Without the curve the answer is 1 and nothing
// is written.
uint32_t __cdecl LevelFourth(char* maps, int32_t x, int32_t z, int32_t* out)
{
    int32_t low = 0, high = 0;
    Virtual<void>(maps, 0x78, uint32_t{5}, &low, &high);
    if (!At<void*>(maps, 0x84))
        return 1;
    void* const grid = Virtual<void*>(Global<void*>(addr::SC4_Service43DA8), 0x24);
    const uint32_t cell = Virtual<uint8_t>(grid, 0x1C, x, z);
    const float here =
        static_cast<float>(This<double>(addr::SC4Curve_Evaluate, At<void*>(maps, 0x84), static_cast<float>(cell)));
    const double next = This<double>(addr::SC4Curve_Evaluate, At<void*>(maps, 0x84), static_cast<float>(cell + 1));
    if (static_cast<double>(low) > static_cast<double>(here)) {
        *out = Ftol(-static_cast<double>(here));
        return static_cast<double>(here) > next ? 2 : 0;
    }
    if (!(static_cast<double>(high) <= static_cast<double>(here)))
        return 1;
    *out = Ftol(static_cast<double>(here) * 0.5);
    return static_cast<double>(here) > next ? 0 : 2;
}

// The game's 0x004C4A30: kind 6, the curve at +0x80, the byte grid of the
// service at 0x00B43DA8 (slot 13), a byte of 0 taken as 0xFF: 2 below, 0
// above.
uint32_t __cdecl LevelFifth(char* maps, int32_t x, int32_t z, int32_t* out)
{
    int32_t low = 0, high = 0;
    Virtual<void>(maps, 0x78, uint32_t{6}, &low, &high);
    if (!At<void*>(maps, 0x80)) {
        *out = 0;
        return 1;
    }
    void* const grid = Virtual<void*>(Global<void*>(addr::SC4_Service43DA8), 0x34);
    uint32_t value = Virtual<uint8_t>(grid, 0x1C, x, z);
    if (value == 0)
        value = 0xFF;
    return Band(Curved(maps, 0x80, static_cast<float>(value)), low, high, out, 2, 0);
}

// --- Registration ----------------------------------------------------------------------

bool InstallQuery1a(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[40] = {};
    static const struct {
        std::uintptr_t address;
        void* ours;
    } hooks[] = {
        {addr::SC4QueryTool_Log, reinterpret_cast<void*>(&Log)},
        {addr::SC4QueryTool_Share, reinterpret_cast<void*>(&Share)},
        {addr::SC4QueryTool_SetChildEdit, reinterpret_cast<void*>(&SetChildEdit)},
        {addr::SC4QueryTool_SetChildText, reinterpret_cast<void*>(&SetChildText)},
        {addr::SC4QueryTool_GetChildNumber, reinterpret_cast<void*>(&GetChildNumber)},
        {addr::SC4QueryTool_SetChildValue, reinterpret_cast<void*>(&SetChildValue)},
        {addr::SC4QueryTool_FillLotShareFirst, reinterpret_cast<void*>(&FillLotShareFirst)},
        {addr::SC4QueryTool_FillCityShare, reinterpret_cast<void*>(&FillCityShare)},
        {addr::SC4QueryTool_FillPollutionPercent, reinterpret_cast<void*>(&FillPollutionPercent)},
        {addr::SC4QueryTool_FillPollutionShare, reinterpret_cast<void*>(&FillPollutionShare)},
        {addr::SC4QueryTool_FillFloatDA0, reinterpret_cast<void*>(&FillFloatDA0)},
        {addr::SC4QueryTool_FillCellShare, reinterpret_cast<void*>(&FillCellShare)},
        {addr::SC4QueryTool_KindOfNumber, reinterpret_cast<void*>(&KindOfNumber)},
        {addr::SC4QueryTool_TypesOfKind, reinterpret_cast<void*>(&TypesOfKind)},
        {addr::SC4QueryTool_LotCapacity, reinterpret_cast<void*>(&LotCapacity)},
        {addr::SC4QueryTool_CellGradeOf, reinterpret_cast<void*>(&CellGradeOf)},
        {addr::SC4QueryTool_LevelText, reinterpret_cast<void*>(&LevelText)},
        {addr::SC4QueryTool_FillWater, reinterpret_cast<void*>(&FillWater)},
        {addr::SC4QueryTool_AttachChildren, reinterpret_cast<void*>(&AttachChildren)},
        {addr::SC4QueryTool_DetachChildren, reinterpret_cast<void*>(&DetachChildren)},
        {addr::SC4QueryTool_LevelFirst, reinterpret_cast<void*>(&LevelFirst)},
        {addr::SC4QueryTool_LevelSecond, reinterpret_cast<void*>(&LevelSecond)},
        {addr::SC4QueryTool_LevelThird, reinterpret_cast<void*>(&LevelThird)},
        {addr::SC4QueryTool_LevelFourth, reinterpret_cast<void*>(&LevelFourth)},
        {addr::SC4QueryTool_LevelFifth, reinterpret_cast<void*>(&LevelFifth)},
        {addr::SC4QueryTool_FillFloatD78, reinterpret_cast<void*>(&FillFloatD78)},
        {addr::SC4QueryTool_FillFloatPolice, reinterpret_cast<void*>(&FillFloatPolice)},
        {addr::SC4QueryTool_FillPoliceShare, reinterpret_cast<void*>(&FillPoliceShare)},
        {addr::SC4QueryTool_FillLotShareSecond, reinterpret_cast<void*>(&FillLotShareSecond)},
        {addr::SC4QueryTool_FillOccupantLotShare, reinterpret_cast<void*>(&FillOccupantLotShare)},
        {addr::SC4QueryTool_FillFloatDA0Other, reinterpret_cast<void*>(&FillFloatDA0Other)},
        {addr::SC4QueryTool_LotPopulation, reinterpret_cast<void*>(&LotPopulation)},
        {addr::SC4QueryTool_LotAverage, reinterpret_cast<void*>(&LotAverage)},
        {addr::SC4QueryTool_FillHistorical, reinterpret_cast<void*>(&FillHistorical)},
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
    "ui.tools.query1a",
    "ui",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4QueryTool_SetChildValue,
    "Our query tool, first piece: the functions its windows are filled with (docs/tools.md)",
    false,
    InstallQuery1a,
});

} // namespace
} // namespace opensc4::tool_query_1a
