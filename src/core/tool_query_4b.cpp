// The query tool of the city view, fourth piece, second half: the fillers
// by cell and their helpers, 0x004CADE0 to 0x004CC6D8.
// Replacement, off by default:
//
//   ui.tools.query4b: 14 functions of the query tool
//     (cSC4ViewInputControlQuery: a view input control of 0xA0 bytes, vtable
//     0x00A90A88; docs/tools.md): the fillers of the query's windows that
//     are given a cell instead of an occupant (0x004CC240, a switch over 22
//     control IDs) and the two walks that run it over a window's children
//     (0x004CC660 with fill, 0x004CC6A0 without); the network pieces' number
//     on a lot, the deals with the neighbour city across an edge (text,
//     amount, cost, kind), the mayor rating through a curve; and two entries
//     of the filler table at 0x00A92488.
//
// Entries of the table that are functions of this file (index, address): 18
// 0x004CC120, 107 0x004CB880 (a function of 0x27 bytes the lister had not
// split from 0x004CB740). None of them changes the city: every function
// here answers true without `fill` (the walk without fill only reaches the
// fillers that read nothing back).
//
// The walks are given to a window's slot 32 by the query's window code
// (0x004CCB3A, 0x004CCE1C, 0x004CCE93, a later piece), which also keeps
// 0x004CC660 at 0x00B21848 for DoMessage (0x004D75B8) to run again.
//
// Nothing is left the game's in this half. Nothing reads a clock or draws a
// random number. The exemplar 0x004CC160 gets from the resource manager is
// never let go: kept, as in the game.
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"
#include "core/tool_query.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>

namespace opensc4::tool_query_4b {
namespace {

namespace cell_query = tool_query::cell_query;

constexpr uint32_t kTextControl = 0x212CDC1F;
constexpr uint32_t kWindowIid = 0x22BA0121;
constexpr uint32_t kTextGroup = 0xEA5524EB;        // the group of the query's game strings
constexpr uint32_t kSecondNumberProperty = 0x27812851;
constexpr uint32_t kRatingCurveProperty = 0xAA60E745; // the curve the mayor rating is shown through
constexpr uint32_t kExemplarIid = 0xA52160F5;
constexpr uint32_t kRatingExemplar[3] = {0x6534284A, 0x67CD5FA1, 0xCA270E9D}; // type, group, instance
constexpr uint32_t kActiveDeal = 2;                // what a deal's flags (slot 38) are asked for
constexpr int32_t kKindThree = 3;                  // 0x004C40B0's kind the deal texts tell apart

// The texts of the deal fillers (game strings of the group 0xEA5524EB).
constexpr uint32_t kDealYes = 0xCA5D4F33;
constexpr uint32_t kDealNo = 0x6A5D4F37;
constexpr uint32_t kDealFirst = 0x0A5D4F4F;      // deals of the kind's first type
constexpr uint32_t kDealFirstThree = 0x8A5D4F4B; // the same for kind 3
constexpr uint32_t kDealSecond = 0x6A5D4F54;     // deals of the kind's second type only
constexpr uint32_t kDealSecondThree = 0xAA5D4F47;

// The float 1 0x004CC210 passes; the curve's default points (0, 0) and
// (256, 256), the bits 0x43800000.
constexpr float kOne = 1.0f;
constexpr float kCurveEnd = 256.0f;

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

// The game's inline destructor (also of a localized string): the vtable
// set to the string's again, the buffer freed.
void Destroy(GameString& text)
{
    text.vtable = addr::SC4String_Vtable;
    if (text.begin)
        Cdecl<void>(addr::SC4_Free, static_cast<void*>(text.begin));
}

// A localized string of the query's group, loaded (0x00603580 with 1).
void MakeLocalized(LocalizedString& text, uint32_t instance)
{
    This<void*>(addr::SC4LocalizedString_SC4LocalizedString, &text, kTextGroup, instance, uint32_t{1});
}

// --- The first three pieces' helpers, by address ------------------------------------------

uint8_t SetChildText(void* window, uint32_t id, void* text)
{
    return Cdecl<uint8_t>(addr::SC4QueryTool_SetChildText, window, id, text);
}

uint8_t SetChildInt(void* window, uint32_t id, int32_t value)
{
    return Cdecl<uint8_t>(addr::SC4QueryTool_SetChildInt, window, id, value);
}

uint8_t SetChildMoney(void* window, uint32_t id, int64_t amount)
{
    return Cdecl<uint8_t>(addr::SC4QueryTool_SetChildMoney, window, id, amount);
}

// The two occupant types of a kind (0x004C4100) of a number (0x004C40B0);
// false for a number without a kind.
uint8_t TypesOf(int32_t kind, uint32_t* first, uint32_t* second)
{
    return Cdecl<uint8_t>(addr::SC4QueryTool_TypesOfKind, kind, first, second);
}

int32_t KindOf(uint32_t number)
{
    return Cdecl<int32_t>(addr::SC4QueryTool_KindOfNumber, number);
}

// The empty game string (0x00A80BF0) as the window's child's caption, as
// the deal fillers end when they find nothing: the window's child as a text
// control (slot 37), its caption (slot 4) set, let go. False without the
// window or the child.
uint8_t CaptionEmpty(void* window, uint32_t id)
{
    GameString empty;
    void* const text = This<void*>(addr::SC4String_FromText, &empty,
                                   reinterpret_cast<const char*>(addr::MySim_ScriptEmpty));
    uint8_t done = 0;
    void* control = nullptr;
    if (window && Virtual<uint8_t>(window, 0x94, id, kTextControl, &control)) {
        Virtual<void>(control, 0x10, text);
        Release(control);
        done = 1;
    }
    Destroy(empty);
    return done;
}

// --- Network pieces on a lot ---------------------------------------------------------------

// The game's 0x004CADE0 (cdecl (window, ID, fill, cell x, cell z); from
// 0x004CC240 for 0x1012 and from 0x004CC120): with `fill`, the lot at the
// cell (the lot manager's slot 6, not referenced; 0 as text without one)
// and the network pieces in its bounding rectangle (lot slot 14; the
// occupant manager's slot 41 into a list of the allocator at 0x00B207E4,
// referenced, the rectangle's corners as two cells, 0 and -1): the first
// numbers of their property 0x27812851 (property holder slot 9, value slot
// 5, numbers slot 105) added up, held between the 32-bit limits, below 0
// made 0, as text. The references are let go (the occupant manager's slot
// 48) before the text, the list freed (0x00413FE0) after.
uint8_t __cdecl FillLotNetworkNumber(void* window, uint32_t id, uint32_t fill, int32_t x, int32_t z)
{
    if (!static_cast<uint8_t>(fill))
        return 1;
    void* const lot = Virtual<void*>(Global<void*>(addr::SC4_LotManager), 0x18, x, z, uint32_t{0});
    if (!lot)
        return SetChildInt(window, id, 0);
    int32_t rect[4] = {};
    Virtual<uint8_t>(lot, 0x38, static_cast<int32_t*>(rect));
    void** const sentinel = static_cast<void**>(Virtual<void*>(ListNodeAllocator(), 0xC, uint32_t{0xC}));
    sentinel[0] = sentinel;
    sentinel[1] = sentinel;
    void* list = sentinel;
    int32_t last[2] = {rect[2], rect[3]};
    int32_t first[2] = {rect[0], rect[1]};
    Virtual<void>(Global<void*>(addr::SC4_NetworkService), 0xA4, &list, static_cast<int32_t*>(first),
                  static_cast<int32_t*>(last), uint32_t{0}, uint32_t{0xFFFFFFFF});
    int32_t sum = 0;
    void** const end = static_cast<void**>(list);
    for (void** node = static_cast<void**>(end[0]); node != end; node = static_cast<void**>(node[0])) {
        void* const property = Virtual<void*>(Virtual<void*>(node[2], 0x18), 0x24, kSecondNumberProperty);
        if (!property)
            continue;
        const int32_t* const numbers = Virtual<const int32_t*>(Virtual<void*>(property, 0x14), 0x1A4);
        const int32_t value = numbers[0];
        const int32_t added = static_cast<int32_t>(static_cast<uint32_t>(value) + static_cast<uint32_t>(sum));
        if (value >= 0)
            sum = added < sum ? INT32_MAX : added;
        else
            sum = added > sum ? INT32_MIN : added;
    }
    Virtual<void>(Global<void*>(addr::SC4_NetworkService), 0xC0, &list);
    if (sum < 0)
        sum = 0;
    const uint8_t done = SetChildInt(window, id, sum);
    This<void>(addr::SC4List_Destroy2, &list);
    return done;
}

// The game's 0x004CC120 (table index 18): the cell at the top left of the
// occupant's bounding cells (slot 11; its answer not looked at, 0 here where
// the game has what was on the stack) to 0x004CADE0. False without an
// occupant, with or without `fill`.
uint8_t __cdecl FillOccupantLotNetworkNumber(void* window, uint32_t id, uint32_t fill, void* occupant)
{
    if (!occupant)
        return 0;
    int32_t cells[4] = {};
    Virtual<uint8_t>(occupant, 0x2C, static_cast<int32_t*>(cells));
    return Cdecl<uint8_t>(addr::SC4QueryTool_FillLotNetworkNumber, window, id, fill, cells[0], cells[1]);
}

// --- Deals with the neighbour across an edge -----------------------------------------------

// What the four deal fillers share: the cell must lie just outside one edge
// of the city (0x006CE7A0 with no city), the neighbours simulator
// (0x00B43D8C) must have a connection of the type there (slot 11: type, x,
// z) and a city across (slot 12: x, z); null otherwise.
void* CityAcross(int32_t x, int32_t z, uint32_t type)
{
    void* const neighbours = Global<void*>(addr::SC4_MonthEndService);
    if (!Cdecl<uint8_t>(addr::SC4_IsOnOneCityEdge, x, z, static_cast<void*>(nullptr)))
        return nullptr;
    if (!Virtual<void*>(neighbours, 0x2C, type, x, z))
        return nullptr;
    return Virtual<void*>(neighbours, 0x30, x, z);
}

// A list of the allocator at 0x00B20D58 (as SC4List::Destroy frees it).
void* NewDealList()
{
    void** const sentinel =
        static_cast<void**>(Virtual<void*>(NodeAllocatorAt(addr::SC4List_NodeAllocator), 0xC, uint32_t{0xC}));
    sentinel[0] = sentinel;
    sentinel[1] = sentinel;
    return sentinel;
}

// The deals with the city across of an occupant type (the neighbours
// simulator's slot 29: type, city, list).
void DealsOf(uint32_t type, void* across, void** list)
{
    Virtual<void>(Global<void*>(addr::SC4_MonthEndService), 0x74, type, across, list);
}

bool Empty(void* list)
{
    return static_cast<void**>(list)[0] == list;
}

// The first deal of the list whose flags (slot 38) have 2; null when none.
void* FirstActive(void* list)
{
    void** const end = static_cast<void**>(list);
    for (void** node = static_cast<void**>(end[0]); node != end; node = static_cast<void**>(node[0])) {
        if (Virtual<uint8_t>(node[2], 0x98, kActiveDeal))
            return node[2];
    }
    return nullptr;
}

// The game's 0x004CAF80 (cdecl (window, ID, fill, x, z, type); from
// 0x004CC240 for 0x104E): with `fill`, whether there is an active deal with
// the city across of the kind of the type (0x004C40B0, 0x004C4100: the
// deals of its first occupant type, or of its second when there are none)
// as one of two game strings; false without a city across. The two types'
// places are not set before (0 here).
uint8_t __cdecl FillNeighbourDealText(void* window, uint32_t id, uint32_t fill, int32_t x, int32_t z, uint32_t type)
{
    if (!static_cast<uint8_t>(fill))
        return 1;
    void* const across = CityAcross(x, z, type);
    if (!across)
        return 0;
    bool active = false;
    uint32_t types[2] = {};
    if (TypesOf(KindOf(type), &types[0], &types[1])) {
        void* list = NewDealList();
        DealsOf(types[0], across, &list);
        if (Empty(list))
            DealsOf(types[1], across, &list);
        active = FirstActive(list) != nullptr;
        This<void>(addr::SC4List_Destroy, &list);
    }
    LocalizedString text;
    MakeLocalized(text, active ? kDealYes : kDealNo);
    const uint8_t done = SetChildText(window, id, &text);
    Destroy(text.text);
    return done;
}

// The game's 0x004CB140 (cdecl (window, ID, fill, x, z, type, scale); from
// 0x004CC1D0 and 0x004CC210): with `fill`, the first active deal's amount
// (slot 10) times the scale, truncated, as text; without a city across, a
// kind or an active deal the empty string as the caption.
uint8_t __cdecl FillNeighbourDealAmount(void* window, uint32_t id, uint32_t fill, int32_t x, int32_t z,
                                        uint32_t type, float scale)
{
    if (!static_cast<uint8_t>(fill))
        return 1;
    uint32_t types[2] = {};
    void* const across = CityAcross(x, z, type);
    if (across && TypesOf(KindOf(type), &types[0], &types[1])) {
        void* list = NewDealList();
        DealsOf(types[0], across, &list);
        if (Empty(list))
            DealsOf(types[1], across, &list);
        if (void* const deal = FirstActive(list)) {
            const int32_t amount = Virtual<int32_t>(deal, 0x28);
            const uint8_t done =
                SetChildInt(window, id, Ftol(static_cast<double>(amount) * static_cast<double>(scale)));
            This<void>(addr::SC4List_Destroy, &list);
            return done;
        }
        This<void>(addr::SC4List_Destroy, &list);
    }
    return CaptionEmpty(window, id);
}

// The game's 0x004CB340 (cdecl (window, ID, fill, x, z, type); from
// 0x004CC240 for 0x1068): with `fill`, the first active deal's cost (slot
// 22, 64 bits) as money; 0 as money otherwise.
uint8_t __cdecl FillNeighbourDealCost(void* window, uint32_t id, uint32_t fill, int32_t x, int32_t z, uint32_t type)
{
    if (!static_cast<uint8_t>(fill))
        return 1;
    uint32_t types[2] = {};
    void* const across = CityAcross(x, z, type);
    if (across && TypesOf(KindOf(type), &types[0], &types[1])) {
        void* list = NewDealList();
        DealsOf(types[0], across, &list);
        if (Empty(list))
            DealsOf(types[1], across, &list);
        if (void* const deal = FirstActive(list)) {
            const uint8_t done = SetChildMoney(window, id, Virtual<int64_t>(deal, 0x58));
            This<void>(addr::SC4List_Destroy, &list);
            return done;
        }
        This<void>(addr::SC4List_Destroy, &list);
    }
    return SetChildMoney(window, id, 0);
}

// The game's 0x004CB4E0 (cdecl (window, ID, fill, x, z, type); from
// 0x004CC240 for 0x1069): with `fill`, a game string for which of the
// kind's two types has deals (the first: 0x0A5D4F4F, for kind 3
// 0x8A5D4F4B; only the second: 0x6A5D4F54, for kind 3 0xAA5D4F47; neither:
// the instance 0) when one of them is active; the empty string as the
// caption otherwise.
uint8_t __cdecl FillNeighbourDealKind(void* window, uint32_t id, uint32_t fill, int32_t x, int32_t z, uint32_t type)
{
    if (!static_cast<uint8_t>(fill))
        return 1;
    void* const across = CityAcross(x, z, type);
    if (across) {
        const int32_t kind = KindOf(type);
        uint32_t types[2] = {};
        if (TypesOf(kind, &types[0], &types[1])) {
            void* list = NewDealList();
            DealsOf(types[0], across, &list);
            uint32_t instance = 0;
            if (!Empty(list)) {
                instance = kind != kKindThree ? kDealFirst : kDealFirstThree;
            } else {
                DealsOf(types[1], across, &list);
                if (!Empty(list))
                    instance = kind != kKindThree ? kDealSecond : kDealSecondThree;
            }
            LocalizedString text;
            MakeLocalized(text, instance);
            if (FirstActive(list)) {
                const uint8_t done = SetChildText(window, id, &text);
                Destroy(text.text);
                This<void>(addr::SC4List_Destroy, &list);
                return done;
            }
            Destroy(text.text);
            This<void>(addr::SC4List_Destroy, &list);
        }
    }
    return CaptionEmpty(window, id);
}

// The game's 0x004CC1D0 (cdecl (window, ID, fill, x, z, type); from
// 0x004CC240 for 0x106A): 0x004CB140 with the pollution simulator's
// garbage scaling factor (slot 48, asked with or without `fill`).
uint8_t __cdecl FillNeighbourDealScaled(void* window, uint32_t id, uint32_t fill, int32_t x, int32_t z,
                                        uint32_t type)
{
    const float scale = Virtual<float>(Global<void*>(addr::SC4_PollutionSimulator), 0xC0);
    return Cdecl<uint8_t>(addr::SC4QueryTool_FillNeighbourDealAmount, window, id, fill, x, z, type, scale);
}

// The game's 0x004CC210 (cdecl (window, ID, fill, x, z, type); from
// 0x004CC240 for 0x1066 and 0x1067): 0x004CB140 with 1.
uint8_t __cdecl FillNeighbourDealCount(void* window, uint32_t id, uint32_t fill, int32_t x, int32_t z,
                                       uint32_t type)
{
    return Cdecl<uint8_t>(addr::SC4QueryTool_FillNeighbourDealAmount, window, id, fill, x, z, type, kOne);
}

// --- The mayor rating ----------------------------------------------------------------------

// The game's STL vector of 8-byte float pairs (begin, end, end of the
// buffer), as the curve takes its points.
struct PointVector {
    float* begin;
    float* end;
    float* capacity;
};
static_assert(sizeof(PointVector) == 0xC);

// SC4Curve: 16 bytes, its segments' buffer first (what the game frees).
struct Curve {
    void* segments;
    uint32_t rest[3];
};
static_assert(sizeof(Curve) == 0x10);

// The game's 0x004CB740 (cdecl (window, ID, fill, property holder, property
// ID); from 0x004CB880 and 0x004CC160): with `fill`, a curve through (0, 0)
// and (256, 256) (0x007C61A0 for each point into an empty vector, the
// second in place when there is room; 0x007D0410, 0x005F4480), replaced by
// the holder's property when it has it (slot 9; 0x005F4620), at the city's
// mayor rating (the aura simulator 0x00B43D60, slot 8, a signed byte),
// truncated, as text. The curve's segments and the points are freed. The
// insert's allocator argument is the address of `fill`, as in the game
// (not read).
uint8_t __cdecl FillMayorRatingCurve(void* window, uint32_t id, uint32_t fill, void* holder, uint32_t property)
{
    if (!static_cast<uint8_t>(fill))
        return 1;
    PointVector points = {};
    float pair[2] = {0.0f, 0.0f};
    This<void>(addr::SC4PointVector_InsertN, &points, static_cast<float*>(nullptr), static_cast<float*>(pair),
               static_cast<void*>(&fill), uint32_t{1}, uint32_t{1});
    pair[0] = kCurveEnd;
    pair[1] = kCurveEnd;
    if (points.end != points.capacity) {
        if (points.end) {
            points.end[0] = kCurveEnd;
            points.end[1] = kCurveEnd;
        }
        points.end += 2;
    } else {
        This<void>(addr::SC4PointVector_InsertN, &points, points.end, static_cast<float*>(pair),
                   static_cast<void*>(&fill), uint32_t{1}, uint32_t{1});
    }
    Curve curve;
    This<void>(addr::SC4Curve_Init, &curve);
    This<uint8_t>(addr::SC4Curve_SetPoints, &curve, &points);
    if (void* const found = Virtual<void*>(holder, 0x24, property))
        This<uint8_t>(addr::SC4Curve_LoadFromProperty, &curve, found);
    const int32_t rating = Virtual<int8_t>(Global<void*>(addr::SC4_Service43D60), 0x20);
    const double curved = This<double>(addr::SC4Curve_Evaluate, &curve, static_cast<float>(rating));
    const uint8_t done = SetChildInt(window, id, Ftol(curved));
    if (curve.segments)
        Cdecl<void>(addr::SC4_Free, curve.segments);
    if (points.begin)
        Cdecl<void>(addr::SC4_Free, static_cast<void*>(points.begin));
    return done;
}

// The game's 0x004CB880 (table index 107): 0x004CB740 with the occupant's
// property holder (slot 6, asked with or without `fill`) and the property
// 0xAA60E745.
uint8_t __cdecl FillOccupantMayorRating(void* window, uint32_t id, uint32_t fill, void* occupant)
{
    void* const holder = Virtual<void*>(occupant, 0x18);
    return Cdecl<uint8_t>(addr::SC4QueryTool_FillMayorRatingCurve, window, id, fill, holder, kRatingCurveProperty);
}

// The game's 0x004CC160 (cdecl (window, ID, fill, x, z; the cell not read);
// from 0x004CC240 for 0x1014 and from 0x004CDC80): the exemplar {0x6534284A,
// 0x67CD5FA1, 0xCA270E9D} from the resource manager (slot 3; never let go)
// and its property holder (slot 7) to 0x004CB740 with 0xAA60E745; false,
// with or without `fill`, when there is no such exemplar.
uint8_t __cdecl FillCityMayorRating(void* window, uint32_t id, uint32_t fill, int32_t, int32_t)
{
    uint32_t key[3] = {kRatingExemplar[0], kRatingExemplar[1], kRatingExemplar[2]};
    void* exemplar = nullptr;
    if (!Virtual<uint8_t>(Global<void*>(addr::SC4_ResourceManager), 0xC, static_cast<uint32_t*>(key), kExemplarIid,
                          &exemplar, uint32_t{0}, uint32_t{0}))
        return 0;
    void* const holder = Virtual<void*>(exemplar, 0x1C);
    return Cdecl<uint8_t>(addr::SC4QueryTool_FillMayorRatingCurve, window, id, fill, holder, kRatingCurveProperty);
}

// --- The fillers by cell -------------------------------------------------------------------

uint8_t Five(std::uintptr_t filler, void* window, uint32_t id, uint32_t fill, const int32_t* query)
{
    return Cdecl<uint8_t>(filler, window, id, fill, query[cell_query::kX / 4], query[cell_query::kZ / 4]);
}

uint8_t Six(std::uintptr_t filler, void* window, uint32_t id, uint32_t fill, const int32_t* query)
{
    return Cdecl<uint8_t>(filler, window, id + static_cast<uint32_t>(query[cell_query::kIdOffset / 4]), fill,
                          query[cell_query::kX / 4], query[cell_query::kZ / 4], query[cell_query::kType / 4]);
}

// The game's 0x004CC240 (cdecl (window, control ID, fill, the cell query)):
// the filler of the control ID, given the cell (and, for the six-argument
// ones, the ID plus the query's +0x8 and the query's type); its answer is
// not kept: false always, also for an ID without one.
uint8_t __cdecl FillCell(void* window, uint32_t id, uint32_t fill, const int32_t* query)
{
    switch (id) {
    case 0x1011:
        Five(addr::SC4QueryTool_FillLotShareSecond, window, id, fill, query);
        break;
    case 0x1012:
        Five(addr::SC4QueryTool_FillLotNetworkNumber, window, id, fill, query);
        break;
    case 0x1013:
        Five(addr::SC4QueryTool_FillAirportCost, window, id, fill, query);
        break;
    case 0x1014:
        Five(addr::SC4QueryTool_FillCityMayorRating, window, id, fill, query);
        break;
    case 0x1015:
        Five(addr::SC4QueryTool_FillLotShareFirst, window, id, fill, query);
        break;
    case 0x1026:
        Five(addr::SC4QueryTool_FillCityShare, window, id, fill, query);
        break;
    case 0x1027:
        Five(addr::SC4QueryTool_FillGarbageLandfilled, window, id, fill, query);
        break;
    case 0x1028:
        Five(addr::SC4QueryTool_FillLandfillCost, window, id, fill, query);
        break;
    case 0x104B:
        Six(addr::SC4QueryTool_FillNeighbourName, window, id, fill, query);
        break;
    case 0x104C:
        Six(addr::SC4QueryTool_FillNeighbourMayor, window, id, fill, query);
        break;
    case 0x104D:
        Six(addr::SC4QueryTool_FillTrafficCellPercent, window, id, fill, query);
        break;
    case 0x104E:
        Six(addr::SC4QueryTool_FillNeighbourDealText, window, id, fill, query);
        break;
    case 0x1066:
    case 0x1067:
        Six(addr::SC4QueryTool_FillNeighbourDealCount, window, id, fill, query);
        break;
    case 0x1068:
        Six(addr::SC4QueryTool_FillNeighbourDealCost, window, id, fill, query);
        break;
    case 0x1069:
        Six(addr::SC4QueryTool_FillNeighbourDealKind, window, id, fill, query);
        break;
    case 0x106A:
        Six(addr::SC4QueryTool_FillNeighbourDealScaled, window, id, fill, query);
        break;
    case 0x1078:
        Five(addr::SC4QueryTool_FillSeaportCost, window, id, fill, query);
        break;
    case 0x107B:
        Five(addr::SC4QueryTool_FillAirportUsageFirst, window, id, fill, query);
        break;
    case 0x107C:
        Five(addr::SC4QueryTool_FillAirportUsageSecond, window, id, fill, query);
        break;
    case 0x107D:
        Five(addr::SC4QueryTool_FillSeaportUsageFirst, window, id, fill, query);
        break;
    case 0x107E:
        Five(addr::SC4QueryTool_FillSeaportUsageSecond, window, id, fill, query);
        break;
    default:
        break;
    }
    return 0;
}

// What the two walks share (cdecl; what a window's slot 32 calls for each
// child: the child, its ID, the child as the interface 0x22BA0121, the
// walk's value, here the cell query): 0x004CC240 for the child; when that
// says no (always) the same walk over the child's children, given the
// game's address of the walk as the game gives it. True.
uint8_t Walk(void* child, uint32_t id, void* window, const int32_t* query, uint32_t fill, std::uintptr_t self)
{
    if (!Cdecl<uint8_t>(addr::SC4QueryTool_FillCell, child, id, fill, query))
        Virtual<void>(window, 0x80, kWindowIid, self, query);
    return 1;
}

// The game's 0x004CC660: the walk with fill.
uint8_t __cdecl FillCellChildren(void* child, uint32_t id, void* window, const int32_t* query)
{
    return Walk(child, id, window, query, 1, addr::SC4QueryTool_FillCellChildren);
}

// The game's 0x004CC6A0: the walk without fill.
uint8_t __cdecl ReadCellChildren(void* child, uint32_t id, void* window, const int32_t* query)
{
    return Walk(child, id, window, query, 0, addr::SC4QueryTool_ReadCellChildren);
}

// --- Registration ----------------------------------------------------------------------

bool InstallQuery4b(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[16] = {};
    static const struct {
        std::uintptr_t address;
        void* ours;
    } hooks[] = {
        {addr::SC4QueryTool_FillLotNetworkNumber, reinterpret_cast<void*>(&FillLotNetworkNumber)},
        {addr::SC4QueryTool_FillNeighbourDealText, reinterpret_cast<void*>(&FillNeighbourDealText)},
        {addr::SC4QueryTool_FillNeighbourDealAmount, reinterpret_cast<void*>(&FillNeighbourDealAmount)},
        {addr::SC4QueryTool_FillNeighbourDealCost, reinterpret_cast<void*>(&FillNeighbourDealCost)},
        {addr::SC4QueryTool_FillNeighbourDealKind, reinterpret_cast<void*>(&FillNeighbourDealKind)},
        {addr::SC4QueryTool_FillMayorRatingCurve, reinterpret_cast<void*>(&FillMayorRatingCurve)},
        {addr::SC4QueryTool_FillOccupantMayorRating, reinterpret_cast<void*>(&FillOccupantMayorRating)},
        {addr::SC4QueryTool_FillOccupantLotNetworkNumber, reinterpret_cast<void*>(&FillOccupantLotNetworkNumber)},
        {addr::SC4QueryTool_FillCityMayorRating, reinterpret_cast<void*>(&FillCityMayorRating)},
        {addr::SC4QueryTool_FillNeighbourDealScaled, reinterpret_cast<void*>(&FillNeighbourDealScaled)},
        {addr::SC4QueryTool_FillNeighbourDealCount, reinterpret_cast<void*>(&FillNeighbourDealCount)},
        {addr::SC4QueryTool_FillCell, reinterpret_cast<void*>(&FillCell)},
        {addr::SC4QueryTool_FillCellChildren, reinterpret_cast<void*>(&FillCellChildren)},
        {addr::SC4QueryTool_ReadCellChildren, reinterpret_cast<void*>(&ReadCellChildren)},
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
    "ui.tools.query4b",
    "ui",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4QueryTool_FillCell,
    "Our query tool, fourth piece, second half: the fillers by cell, the neighbour deals and the mayor rating "
    "(docs/tools.md)",
    false,
    InstallQuery4b,
});

} // namespace
} // namespace opensc4::tool_query_4b
