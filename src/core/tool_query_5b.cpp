// The query tool of the city view, fifth piece, second half: the debug
// texts and the development log, 0x004CCF60 to 0x004CDC65 and 0x004CDF80
// to 0x004CF89C.
// Replacement, off by default:
//
//   ui.tools.query5b: 7 functions of the query tool
//     (cSC4ViewInputControlQuery: a view input control of 0xA0 bytes, vtable
//     0x00A90A88; docs/tools.md): the texts the query appends to a string
//     for its debug view (a building's capacities and plant figures
//     0x004CCF60, the growth developers' demand at a cell 0x004CD820, a
//     cell's zone, land value, pollution and traffic 0x004CE8A0, the
//     developer shares of a lot 0x004CF2B0), the development outcome's
//     name (0x004CDF80, register convention: index in EAX, string in ESI;
//     hooked through a naked thunk) and the development log (0x004CE320,
//     0x004CE440) that goes to the query's log lines (0x004C3750).
//
// Nothing here changes the city: every function only reads simulators and
// writes strings or log lines. Nothing draws a random number or reads a
// clock. 0x004CE440 makes a function-local static string on its first
// call (the game's own: 0x00B218EC, guard 0x00B21900 bit 1, its exit
// handler 0x00A7B150 given to atexit 0x009EF059), as in the game.
// 0x004CE8A0 reads one float of its frame that nothing sets (the sixth
// route kind's volume, shown only when the sixth capacity is not 0): 0
// here.
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

namespace opensc4::tool_query_5b {
namespace {

namespace record_field = tool_query::development_record;
namespace attempt_field = tool_query::development_attempt;
namespace candidate_field = tool_query::development_candidate;

constexpr uint32_t kBuildingIid = 0x87DDA3A9;  // the building occupant's interface
constexpr uint32_t kTypeNetwork = 0xC772BF98;  // the network occupants' type

// The pollution simulator's plant figures (slot 53: kind, occupant, out)
// and the properties with what is used of them.
constexpr uint32_t kIncinerator = 0xA8EBCF0C;
constexpr uint32_t kIncineratorUsed = 0xA8FCDBA8;
constexpr uint32_t kWasteToEnergy = 0x48EBD0B1;
constexpr uint32_t kWasteToEnergyUsed = 0x28FCDBB0;
constexpr uint32_t kMaxPower = 0x08FD1613;
constexpr uint32_t kWaterTreatment = 0xC8EBD131;
constexpr uint32_t kWaterTreatmentUsed = 0x08FCDB69;
constexpr uint32_t kWaterConsumed = 0xC8ED2D84;
constexpr uint32_t kWaterProduced = 0x88ED3303;
constexpr uint32_t kAgeDegradation = 0xC910B4CF;

// The floats 100 (0x00A80AA0) and 12582912 (0x00A90978, 1.5 * 2^23: added
// so that the float's low bits are the rounded number) and the double 100
// (0x00A80AC0).
constexpr float kHundred = 100.0f;
constexpr float kRoundingBias = 12582912.0f;
constexpr uint32_t kRoundingBits = 0xB4C00000u; // added to the float's bits: less 0x4B400000
constexpr double kHundredDouble = 100.0;

// The growth developer types of the demand text, by zone (the zone
// manager's slot 12, less 1: 0 to 2 residential, 3 and 4 commercial, 5 to 8
// industrial).
constexpr uint32_t kResidential[3] = {0x1010, 0x1020, 0x1030};
constexpr uint32_t kCommercial[5] = {0x3110, 0x3120, 0x3130, 0x3320, 0x3330};
constexpr uint32_t kIndustrial[4] = {0x4100, 0x4200, 0x4300, 0x4400};

// The development outcomes' names (0x004CDF80, 28 of them).
constexpr const char* kOutcomeNames[tool_query::kOutcomeCount] = {
    "unknown failure",
    "nothing to reoccupy",
    "monopoly piece",
    "no lot",
    "lot not ready",
    "no lot configuration",
    "wrong zone",
    "wrong purpose",
    "wrong wealth",
    "wrong style",
    "too steep",
    "no building big enough",
    "stage too small",
    "all buildings too big",
    "stage too big",
    "resident has different purpose",
    "resident has different wealth",
    "not bigger than existing stage",
    "historical",
    "just built recently",
    "not enough construction resources",
    "poor roads",
    "wrong land value",
    "no fire coverage",
    "no park",
    "no water",
    "no power",
    "success",
};

// The developer types' short names. `assign` tells which of them
// 0x004CF2B0 copies through Assign (0x00424380) rather than the body's
// AssignRange (0x00408A70); both give the same string.
constexpr struct {
    uint32_t type;
    const char* name;
    bool assign;
} kDeveloperNames[] = {
    {0x1010, "R$", true},     {0x1020, "R$$", true},    {0x1030, "R$$$", true}, {0x3110, "Cs$", false},
    {0x3120, "Cs$$", true},   {0x3130, "Cs$$$", true},  {0x3320, "Co$$", false}, {0x3330, "Co$$$", false},
    {0x4100, "Ir", true},     {0x4200, "Id", false},    {0x4300, "Im", false},   {0x4400, "Ih", false},
};

// The zone names of the cell text, by zone type 0 to 15.
constexpr const char* kZoneNames[16] = {"None",     "R#",      "R##",     "R###",      "C#",      "C##",
                                        "C###",     "Ir",      "Il",      "Ih",        "Military", "Airport",
                                        "Seaport",  "Spaceport", "Landfill", "Plopped Bldg"};

// The route kinds' short names of the cell text (0x00B0972C, six by
// capacity; 0x00B09708, nine by volume).
constexpr const char* kCapacityNames[6] = {"Wk", "Rd", "Rl", "Su", "El", "Mo"};
constexpr const char* kVolumeNames[9] = {"Wk", "Ca", "Bu", "Pt", "Tr", "Ft", "Su", "El", "Mo"};

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

// The query's log line (0x004C3750, by address: ours when query1a is on).
template <typename... Args>
void Log(const char* format, Args... args)
{
    Cdecl<void>(addr::SC4QueryTool_Log, format, args...);
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

// What the texts append: a string's body to the body `to` (0x004081D0),
// or a C string's characters (0x0040EBC0). The allocator argument is not
// read.
void AppendString(char** to, const GameString& text, void* allocator)
{
    This<void>(addr::SC4String_AppendRange, to, text.begin, text.end, allocator);
}

void AppendText(char** to, const char* text, void* allocator)
{
    This<void>(addr::cSC4Advisor_F40EBC0, to, text, text + std::strlen(text), allocator);
}

// The body of an SC4String the caller passed (its +4).
char** BodyOf(void* text)
{
    return reinterpret_cast<char**>(static_cast<char*>(text) + 0x4);
}

// A C string's characters as a string's body, through a string made for it
// (0x00408480) whose buffer is freed after (its vtable not set again, as in
// the game).
void AssignBody(GameString& to, const char* text, void* allocator)
{
    GameString made;
    This<void*>(addr::SC4String_FromText, &made, text);
    char* const begin = made.begin;
    This<void>(addr::SC4String_AssignRange, &to.begin, begin, made.end, allocator);
    if (begin)
        Cdecl<void>(addr::SC4_Free, static_cast<void*>(begin));
}

// A C string through Assign (0x00424380) from a string made for it, which
// is then destroyed.
void AssignWhole(void* to, const char* text)
{
    GameString made;
    This<void*>(addr::SC4String_FromText, &made, text);
    This<void>(addr::cRZBaseString_Assign, to, &made);
    Destroy(made);
}

// The next node of a red-black tree in order (STLport's increment, with
// the header as the end): +0x4 parent, +0x8 left, +0xC right.
char* TreeNext(char* node)
{
    if (char* right = At<char*>(node, 0xC)) {
        while (At<char*>(right, 0x8))
            right = At<char*>(right, 0x8);
        return right;
    }
    char* current = node;
    char* parent = At<char*>(node, 0x4);
    if (current == At<char*>(parent, 0xC)) {
        do {
            current = parent;
            parent = At<char*>(parent, 0x4);
        } while (current == At<char*>(parent, 0xC));
    }
    if (At<char*>(current, 0xC) != parent)
        current = parent;
    return current;
}

// What property value an occupant's holder gives (slot 9, the value's slot
// 5) as a whole number (slot 72); null property: false.
bool PropertyNumber(void* holder, uint32_t id, uint32_t& number)
{
    void* const property = Virtual<void*>(holder, 0x24, id);
    if (!property)
        return false;
    number = Virtual<uint32_t>(Virtual<void*>(property, 0x14), 0x120);
    return true;
}

// --- A building's figures ------------------------------------------------------------------

// One of the plant figures 0x004CCF60 shows: the pollution simulator's
// slot 53 (kind, occupant, capacity); with it "<what> %d\n" and, when the
// capacity is not 0 and the holder has the property, "<used> %d%%\n" with
// the property's number * 100 / the capacity (32 bits, unsigned). `more`
// runs between the two, inside the same string's life.
template <typename More>
void PlantFigure(void* occupant, void* holder, char** body, void* allocator, uint32_t kind, uint32_t used,
                 const char* capacityText, const char* usedText, More more)
{
    uint32_t capacity = 0;
    if (!Virtual<uint8_t>(Global<void*>(addr::SC4_PollutionSimulator), 0xD4, kind, occupant, &capacity))
        return;
    GameString text;
    MakeEmpty(text);
    Cdecl<void>(addr::cRZBaseString_Sprintf, &text, capacityText, capacity);
    AppendString(body, text, allocator);
    void* const property = Virtual<void*>(holder, 0x24, used);
    if (capacity != 0 && property) {
        const uint32_t number = Virtual<uint32_t>(Virtual<void*>(property, 0x14), 0x120);
        Cdecl<void>(addr::cRZBaseString_Sprintf, &text, usedText, number * 100u / capacity);
        AppendString(body, text, allocator);
    }
    more(text);
    Destroy(text);
}

// The game's 0x004CCF60 (cdecl (occupant, text); callers 0x004D4B15 and
// 0x004D5EC5): appends to the text what the occupant is: for a building
// (QueryInterface 0x87DDA3A9, its slot 11's record) its capacity (+0x8,
// when not 0) and wealth (+0x4: 1 to 3); the power simulator's figures
// (slot 32 consumed; slot 33 produced and used, as a share when produced
// is not 0); the pollution simulator's plant figures (incinerator, waste
// to energy with the property 0x08FD1613, water treatment; slot 55 the
// recycling reduction, rounded through the float trick); the holder's
// water consumed (with the water simulator's slot 9: watered or not),
// water produced (with slot 10) and age degradation (a float above 0).
// Values the game does not set before a call are 0 here.
void __cdecl AppendBuildingText(void* occupant, void* text)
{
    char** const body = BodyOf(text);
    void* const allocator = &occupant;
    void* building = nullptr;
    if (Virtual<uint8_t>(occupant, 0x0, kBuildingIid, &building)) {
        const char* const record = Virtual<const char*>(building, 0x2C);
        if (At<uint16_t>(const_cast<char*>(record), 0x8) != 0) {
            GameString line;
            MakeEmpty(line);
            Cdecl<void>(addr::cRZBaseString_Sprintf, &line, "Capacity: %d\n",
                        uint32_t{At<uint16_t>(const_cast<char*>(record), 0x8)});
            AppendString(body, line, allocator);
            Destroy(line);
        }
        switch (At<int32_t>(const_cast<char*>(record), 0x4)) {
        case 1:
            AppendText(body, "Wealth $\n", allocator);
            break;
        case 2:
            AppendText(body, "Wealth $$\n", allocator);
            break;
        case 3:
            AppendText(body, "Wealth $$$\n", allocator);
            break;
        default:
            break;
        }
        Release(building);
    }

    void* const power = Global<void*>(addr::SC4_Service43DA0);
    uint32_t consumed = 0;
    if (Virtual<uint8_t>(power, 0x80, occupant, &consumed)) {
        GameString line;
        MakeEmpty(line);
        Cdecl<void>(addr::cRZBaseString_Sprintf, &line, "Power consumed: %d\n", consumed);
        AppendString(body, line, allocator);
        Destroy(line);
    }
    uint32_t produced = 0, used = 0;
    if (Virtual<uint8_t>(power, 0x84, occupant, &produced, &used)) {
        GameString line;
        MakeEmpty(line);
        Cdecl<void>(addr::cRZBaseString_Sprintf, &line, "Power produced: %d\n", produced);
        AppendString(body, line, allocator);
        if (produced != 0) {
            Cdecl<void>(addr::cRZBaseString_Sprintf, &line, "Power capacity used: %d%%\n", used * 100u / produced);
            AppendString(body, line, allocator);
        }
        Destroy(line);
    }

    void* const holder = Virtual<void*>(occupant, 0x18);
    const auto nothing = [](GameString&) {};
    PlantFigure(occupant, holder, body, allocator, kIncinerator, kIncineratorUsed, "Incinerator Capacity: %d\n",
                "Incinerator capacity used: %d%%\n", nothing);
    PlantFigure(occupant, holder, body, allocator, kWasteToEnergy, kWasteToEnergyUsed,
                "Waste to energy capacity: %d\n", "Waste to energy capacity used: %d%%\n", [&](GameString& line) {
                    uint32_t most = 0;
                    if (Cdecl<uint8_t>(addr::SC4_GetPropertyUint32, holder, kMaxPower, &most)) {
                        Cdecl<void>(addr::cRZBaseString_Sprintf, &line, "Max power generated: %d%\n", most);
                        AppendString(body, line, allocator);
                    }
                });
    PlantFigure(occupant, holder, body, allocator, kWaterTreatment, kWaterTreatmentUsed,
                "Water treatment capacity: %d\n", "Water treatment capacity used: %d%%\n", nothing);

    void* const pollution = Global<void*>(addr::SC4_PollutionSimulator);
    float reduction = 0.0f;
    if (Virtual<uint8_t>(pollution, 0xDC, occupant, &reduction)) {
        GameString line;
        MakeEmpty(line);
        const float biased = static_cast<float>(static_cast<double>(reduction) * static_cast<double>(kHundred) +
                                                static_cast<double>(kRoundingBias));
        uint32_t bits = 0;
        std::memcpy(&bits, &biased, sizeof bits);
        Cdecl<void>(addr::cRZBaseString_Sprintf, &line, "Recycling center %% reduction: %d%%\n", bits + kRoundingBits);
        AppendString(body, line, allocator);
        Destroy(line);
    }

    uint32_t water = 0;
    if (PropertyNumber(holder, kWaterConsumed, water) && water > 0) {
        void* const plumbing = Global<void*>(addr::SC4_Service43D94);
        GameString line;
        MakeEmpty(line);
        Cdecl<void>(addr::cRZBaseString_Sprintf, &line, "Water consumed: %d", water);
        AppendString(body, line, allocator);
        if (Virtual<uint8_t>(plumbing, 0x24, occupant))
            AppendText(body, " (watered)\n", allocator);
        else
            AppendText(body, " (unwatered)\n", allocator);
        Destroy(line);
    }
    uint32_t made = 0;
    if (PropertyNumber(holder, kWaterProduced, made) && made > 0) {
        void* const plumbing = Global<void*>(addr::SC4_Service43D94);
        GameString line;
        MakeEmpty(line);
        const uint32_t producing = Virtual<uint32_t>(plumbing, 0x28, occupant);
        Cdecl<void>(addr::cRZBaseString_Sprintf, &line, "Water Produced: %d//%d\n", producing, made);
        AppendString(body, line, allocator);
        Destroy(line);
    }
    if (void* const property = Virtual<void*>(holder, 0x24, kAgeDegradation)) {
        const float age = Virtual<float>(Virtual<void*>(property, 0x14), 0x140);
        if (age > 0.0f) {
            GameString line;
            MakeEmpty(line);
            Cdecl<void>(addr::cRZBaseString_Sprintf, &line, "Age Degradation: %02.2f%%\n",
                        static_cast<double>(age) * kHundredDouble);
            AppendString(body, line, allocator);
            Destroy(line);
        }
    }
}

// --- The growth developers' demand -----------------------------------------------------------

// A developer's map value at the cell (the developer's map at `slot`, its
// slot 7).
uint32_t MapAt(void* developer, std::size_t slot, int32_t x, int32_t z)
{
    return Virtual<uint32_t>(Virtual<void*>(developer, slot), 0x1C, x, z);
}

// The game's 0x004CD820 (cdecl (cell x, cell z, text); callers 0x004D4B55,
// 0x004D4BE4 and 0x004D5F1E): by the zone at the cell (the zone manager's
// slot 12) the demand of its growth developers (the building development
// service's slot 32 by type): residential and industrial each three maps
// (slots 16 and 14 unsigned, 13 signed, 16 bits), commercial its slot 13
// map (signed); formatted (a string the text's body gets appended); for a
// zone without developers an empty string.
void __cdecl AppendDemandText(int32_t x, int32_t z, void* text)
{
    void* const development = Global<void*>(addr::SC4_Service43D68);
    GameString line;
    MakeEmpty(line);
    const uint32_t zone = Virtual<uint32_t>(Global<void*>(addr::SC4_Service43D14), 0x30, x, z);
    const auto developer = [&](uint32_t type) { return Virtual<void*>(development, 0x80, type); };
    switch (zone) {
    case 1:
    case 2:
    case 3: {
        int32_t v[9];
        for (int i = 0; i < 3; ++i) {
            void* const d = developer(kResidential[i]);
            v[3 * i] = static_cast<uint16_t>(MapAt(d, 0x40, x, z));
            v[3 * i + 1] = static_cast<uint16_t>(MapAt(d, 0x38, x, z));
            v[3 * i + 2] = static_cast<int16_t>(MapAt(d, 0x34, x, z));
        }
        Cdecl<void>(addr::cRZBaseString_Sprintf, &line,
                    "R demand mc$:%d, c$:%d, d$:%d\nR demand mc$$:%d, c$$:%d, d$$:%d\nR demand mc$$$:%d, c$$$:%d, "
                    "d$$$:%d\n",
                    v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7], v[8]);
        break;
    }
    case 4:
    case 5: {
        int32_t v[5];
        for (int i = 0; i < 5; ++i)
            v[i] = static_cast<int16_t>(MapAt(developer(kCommercial[i]), 0x34, x, z));
        Cdecl<void>(addr::cRZBaseString_Sprintf, &line,
                    "Cs demand: d$:%d; d$$:%d; d$$$:%d\nCo demand d$$:%d; d$$$:%d\n", v[0], v[1], v[2], v[3], v[4]);
        break;
    }
    case 6:
    case 7:
    case 8:
    case 9: {
        int32_t v[12];
        for (int i = 0; i < 4; ++i) {
            void* const d = developer(kIndustrial[i]);
            v[3 * i] = static_cast<uint16_t>(MapAt(d, 0x40, x, z));
            v[3 * i + 1] = static_cast<uint16_t>(MapAt(d, 0x38, x, z));
            v[3 * i + 2] = static_cast<int16_t>(MapAt(d, 0x34, x, z));
        }
        Cdecl<void>(addr::cRZBaseString_Sprintf, &line,
                    "I Demand mc$:%d, c$:%d, d$:%d\nI Demand mc$$d:%d, c$$d:%d, d$$d:%d\nI Demand mc$$m:%d, "
                    "c$$m:%d, d$$m:%d; mc$$$:%d, c$$$:%d, d$$$:%d\n",
                    v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7], v[8], v[9], v[10], v[11]);
        break;
    }
    default:
        break;
    }
    AppendString(BodyOf(text), line, &text);
    Destroy(line);
}

// --- The development log ---------------------------------------------------------------------

// The game's 0x004CDF80 as plain C++ (index, string): the outcome's name
// assigned to the string (0x00408480 into a string of the frame, Assign
// 0x00424380, destroyed); nothing for an index past 27.
void __cdecl AssignOutcomeName(uint32_t index, void* text)
{
    if (index >= tool_query::kOutcomeCount)
        return;
    AssignWhole(text, kOutcomeNames[index]);
}

// The game's 0x004CDF80 (index in EAX, the string in ESI; callers 0x004CE3A0
// and 0x004CE828, both in this file, which call AssignOutcomeName itself).
// Changes EAX, ECX and EDX, as the game's.
__declspec(naked) void OutcomeNameThunk()
{
    __asm push esi
    __asm push eax
    __asm call AssignOutcomeName
    __asm add esp, 8
    __asm ret
}

// The game's 0x004CE320 (cdecl (record); caller 0x004CE440): the record's
// outcome counts (+0x34: 13 rows of 28) as a table on the log, one line
// per outcome ("     . " for 0).
void __cdecl LogDevelopmentFailures(char* record)
{
    static const char kPrefix[] = "|   ";
    char* prefix[3] = {};
    uint8_t allocator = 0;
    This<void>(addr::SC4String_FromRange, static_cast<void*>(prefix), &kPrefix[0], &kPrefix[4], &allocator);
    char* const lead = prefix[0];
    Log("%s\n", lead);
    Log("%s%33s _Grow_ _TOA__ _LPLC_ _LCLE_ _LCLA_ _LCLS_ _Bld__ _ILCS_ _ILS__ _IAL__ __PB__ _ILCC_ _IBC__\n", lead,
        reinterpret_cast<const char*>(addr::MySim_ScriptEmpty));
    for (uint32_t outcome = 0; outcome < tool_query::kOutcomeCount; ++outcome) {
        GameString name;
        MakeEmpty(name);
        AssignOutcomeName(outcome, &name);
        char* const begin = name.begin;
        Log("%s%33s ", lead, begin);
        for (std::size_t row = 0; row < record_field::kFailureRows; ++row) {
            const uint32_t* const counts = At<uint32_t*>(At<char*>(record, record_field::kFailures), row * 0xC);
            const uint32_t count = counts[outcome];
            if (count != 0)
                Log("%6d ", count);
            else
                Log("     . ");
        }
        Log("\n");
        name.vtable = addr::SC4String_Vtable;
        if (begin)
            Cdecl<void>(addr::SC4_Free, static_cast<void*>(begin));
    }
    if (lead)
        Cdecl<void>(addr::SC4_Free, static_cast<void*>(lead));
}

// The short name of a developer type for the log (0x004CE440's switch);
// null for another.
const char* DeveloperName(uint32_t type)
{
    for (const auto& entry : kDeveloperNames) {
        if (entry.type == type)
            return entry.name;
    }
    return nullptr;
}

// The game's 0x004CE440 (cdecl (record); caller 0x004D12A0 with the tract
// developer's records, 0x00705E90 / 0x00705FA0): on its first call the
// static string "unknown building" (0x00B218EC). Then, with a record, on
// the log: its day (+0x0 slot 2), the developer type (+0x8 slot 6), the
// tract and its figures, the outcome table (0x004CE320), and every build
// attempt: fussy or not, the lot sizes (a tree of objects whose slot 7
// gives the two sides), at most ten candidates ("..." when the tenth is
// not the last; the score, a float, passed as a double to a %d, as in the
// game), the capacities wanted and the lot configuration chosen (the
// building development service's slot 38 for its name, slot 39's +0x8 for
// its capacity; the static string and 0 without one), the outcome's name.
void __cdecl LogDevelopmentAttempt(char* record)
{
    uint32_t& guard = Global<uint32_t>(addr::SC4QueryTool_UnknownBuildingGuard);
    if (!(static_cast<uint8_t>(guard) & 1)) {
        guard |= 1;
        This<void*>(addr::SC4String_FromText, reinterpret_cast<void*>(addr::SC4QueryTool_UnknownBuilding),
                    "unknown building");
        Cdecl<int32_t>(addr::SC4_atexit, addr::SC4QueryTool_UnknownBuildingExit);
    }
    if (!record)
        return;
    void* const development = Global<void*>(addr::SC4_Service43D68);
    GameString date;
    MakeEmpty(date);
    Virtual<void>(record, 0x8, &date, uint32_t{0});
    Log("|   Sim date %s\n", date.begin);
    Log("|   Developer type ");
    const uint32_t type = Virtual<uint32_t>(At<void*>(record, record_field::kDeveloper), 0x18);
    if (const char* const name = DeveloperName(type))
        Log(name);
    Log("\n");
    Log("|   Tract (%d, %d)\n", At<int32_t>(record, record_field::kTractX), At<int32_t>(record, record_field::kTractZ));
    Log("|   Population:             %d\n", At<int32_t>(record, record_field::kPopulation));
    Log("|   Existing capacity:      %d\n", At<int32_t>(record, record_field::kCapacity));
    Log("|   Maximum capacity:       %d\n", At<int32_t>(record, record_field::kMaxCapacity));
    Log("|   Desirability:           %d\n", At<int32_t>(record, record_field::kDesirability));
    Log("|   Construction Resources: %d\n", At<int32_t>(record, record_field::kResources));
    Cdecl<void>(addr::SC4QueryTool_LogDevelopmentFailures, record);

    for (char* attempt = At<char*>(At<char*>(record, record_field::kAttempts), 0x0);
         attempt != At<char*>(record, record_field::kAttempts); attempt = At<char*>(attempt, 0x0)) {
        Log("|   \n");
        Log("|   Build attempt (%s):\n", At<uint8_t>(attempt, attempt_field::kNotFussy) ? "not fussy" : "fussy");
        Log("|     Lot sizes:");
        char* const header = At<char*>(attempt, attempt_field::kLotSizes);
        for (char* node = At<char*>(header, 0x8); node != At<char*>(attempt, attempt_field::kLotSizes);
             node = TreeNext(node)) {
            int32_t first = 0, second = 0;
            Virtual<void>(At<void*>(node, 0x10), 0x1C, &first, &second);
            Log(" %dx%d", first, second);
        }
        Log("\n");
        Log("|     Candidates (in order of preference):\n");
        char** candidate = At<char**>(attempt, attempt_field::kCandidates);
        if (candidate != At<char**>(attempt, attempt_field::kCandidatesEnd)) {
            int32_t shown = 0;
            for (;;) {
                char* const c = *candidate;
                Log("|       ");
                switch (At<uint32_t>(c, candidate_field::kKind)) {
                case 1:
                    Log("Refurbish");
                    break;
                case 2:
                    Log("Build new");
                    break;
                case 3:
                    Log("Aggregate");
                    break;
                case 4:
                    Log("Subdivide");
                    break;
                default:
                    break;
                }
                Log(" (%d, %d) %dx%d, existing capacity %d (+ %d abandoned), score:%d\n",
                    At<int32_t>(c, candidate_field::kX), At<int32_t>(c, candidate_field::kZ),
                    uint32_t{At<uint8_t>(c, candidate_field::kSizeX)},
                    uint32_t{At<uint8_t>(c, candidate_field::kSizeZ)},
                    At<int32_t>(c, candidate_field::kCapacity), At<int32_t>(c, candidate_field::kAbandoned),
                    static_cast<double>(At<float>(c, candidate_field::kScore)));
                if (++shown >= 10)
                    break;
                ++candidate;
                if (candidate == At<char**>(attempt, attempt_field::kCandidatesEnd))
                    break;
            }
            if (candidate != At<char**>(attempt, attempt_field::kCandidatesEnd))
                Log("|       ...\n");
        }
        const uint32_t config = At<uint32_t>(attempt, attempt_field::kLotConfig);
        void* const building = config ? Virtual<void*>(development, 0x98, config)
                                      : reinterpret_cast<void*>(addr::SC4QueryTool_UnknownBuilding);
        // The game reads the configuration again for the capacity.
        const uint32_t again = At<uint32_t>(attempt, attempt_field::kLotConfig);
        const uint32_t capacity = again ? uint32_t{At<uint16_t>(Virtual<char*>(development, 0x9C, again), 0x8)} : 0;
        Log("|     Want building capacity [%d, %d]. Selected %s (capacity %d)\n",
            At<int32_t>(attempt, attempt_field::kWantMin), At<int32_t>(attempt, attempt_field::kWantMax),
            Virtual<const char*>(building, 0x14), capacity);
        GameString outcome;
        MakeEmpty(outcome);
        AssignOutcomeName(At<uint32_t>(attempt, attempt_field::kOutcome), &outcome);
        char* const begin = outcome.begin;
        Log("|     Outcome: %s\n", begin);
        outcome.vtable = addr::SC4String_Vtable;
        if (begin)
            Cdecl<void>(addr::SC4_Free, static_cast<void*>(begin));
    }
    Destroy(date);
}

// --- A cell's figures ---------------------------------------------------------------------

// The game's 0x004CE8A0 (cdecl (zone type, point, text); callers 0x004D4C9B
// and 0x004D5F42): for the cell of the point (+0x0 x, +0x8 z; city slot 95)
// a text appended to the text's body: the zone's name ("Unknown" past 15),
// what the text calls land value (the service 0x00B43D88: its slot 7's
// map and slot 6, bytes) and its grade (slot 5: 1 to 3, Low to High), the
// terrain's height (slot 13, truncated), flammability
// (0x00B43D7C slot 5's map), powered or not (power slot 19), the pollution
// simulator's air, water and garbage (slots 7 to 9) and radiation (slot
// 16); for a landfill (14) the used share of its capacity (0x00B43D28 slot
// 10, not times 100, as in the game); for the network piece there (the
// network occupants' slot 33) the traffic simulator's capacities (slot 23)
// and volumes (slot 12, both directions) by route kind; for a lot the trip
// length (traffic slots 13's map and 14) and, without a travel designate
// (lot slot 59), its jobs (traffic slot 20 by wealth).
void __cdecl AppendCellText(uint32_t zone, const float* point, void* text)
{
    int32_t cellX = 0, cellZ = 0;
    Virtual<uint8_t>(Global<void*>(addr::SC4_City), 0x17C, point[0], point[2], &cellX, &cellZ);
    uint8_t allocator = 0;
    GameString line;
    MakeEmpty(line);
    void* const landValue = Global<void*>(addr::SC4_Service43D88);
    const uint32_t value = Virtual<uint8_t>(Virtual<void*>(landValue, 0x1C), 0x1C, cellX, cellZ);
    const uint32_t second = Virtual<uint8_t>(landValue, 0x18, cellX, cellZ);
    const int32_t wealth = Virtual<int32_t>(landValue, 0x14, cellX, cellZ);
    GameString grade;
    MakeEmpty(grade);
    if (wealth == 1)
        AssignBody(grade, "Low", &allocator);
    else if (wealth == 2)
        AssignBody(grade, "Medium", &allocator);
    else if (wealth == 3)
        AssignBody(grade, "High", &allocator);
    static const char kUnknown[] = "Unknown";
    GameString zoneName;
    zoneName.begin = nullptr;
    zoneName.end = nullptr;
    zoneName.capacity = nullptr;
    This<void>(addr::SC4String_FromRange, &zoneName.begin, &kUnknown[0], &kUnknown[7], &allocator);
    zoneName.vtable = addr::SC4String_Vtable;
    zoneName.zero = 0;
    if (zone <= 15)
        AssignWhole(&zoneName, kZoneNames[zone]);
    const int32_t altitude = Ftol(Virtual<double>(Global<void*>(addr::SC4_Terrain), 0x34, point[0], point[2]));
    GameString powered;
    MakeEmpty(powered);
    if (Virtual<uint8_t>(Global<void*>(addr::SC4_Service43DA0), 0x4C, cellX, cellZ))
        AssignBody(powered, "Powered", &allocator);
    else
        AssignBody(powered, "Unpowered", &allocator);
    void* const pollution = Global<void*>(addr::SC4_PollutionSimulator);
    int32_t air = 0, water = 0, garbage = 0;
    Virtual<uint8_t>(pollution, 0x1C, cellX, cellZ, &air);
    Virtual<uint8_t>(pollution, 0x20, cellX, cellZ, &water);
    Virtual<uint8_t>(pollution, 0x24, cellX, cellZ, &garbage);
    const uint32_t flammability =
        Virtual<uint8_t>(Virtual<void*>(Global<void*>(addr::SC4_Service43D7C), 0x14), 0x1C, cellX, cellZ);
    const uint32_t radiation = Virtual<uint8_t>(pollution, 0x40, cellX, cellZ) != 0 ? 1u : 0u;
    Cdecl<void>(addr::cRZBaseString_Sprintf, &line,
                "Zoned: %s\nLand Value %d:%d (%s)\nAlt: %d Flam: %d\n%s\nPollution: Air:%d Water:%d Garbage:%d "
                "Rad?:%d",
                zoneName.begin, value, second, grade.begin, altitude, flammability, powered.begin, air, water,
                garbage, radiation);

    if (zone == 14) {
        GameString used;
        MakeEmpty(used);
        uint32_t full = 0, capacity = 0;
        Virtual<void>(Global<void*>(addr::SC4_Value43D28), 0x28, &full, &capacity);
        Cdecl<void>(addr::cRZBaseString_Sprintf, &used, "Used landfill capacity: %.0f%%\n",
                    static_cast<double>(full) / static_cast<double>(capacity));
        AppendString(&line.begin, used, &allocator);
        Destroy(used);
    }

    void* network = nullptr;
    if (Virtual<uint8_t>(Global<void*>(addr::SC4_NetworkService), 0x84, &network, cellX, cellZ, kTypeNetwork)) {
        GameString figures;
        MakeEmpty(figures);
        void* const traffic = Global<void*>(addr::SC4_Service43DA8);
        float capacity[6];
        capacity[0] = 0.0f;
        for (uint32_t kind = 1; kind < 6; ++kind)
            capacity[kind] = Virtual<float>(traffic, 0x5C, kind, cellX, cellZ);
        float volume[9];
        for (uint32_t kind = 0; kind < 9; ++kind) {
            const char* const out = Virtual<const char*>(traffic, 0x30, cellX, cellZ, kind, uint32_t{0});
            const char* const back = Virtual<const char*>(traffic, 0x30, cellX, cellZ, kind, uint32_t{1});
            const int32_t sum =
                At<uint16_t>(const_cast<char*>(back), 0x20) + At<uint16_t>(const_cast<char*>(out), 0x20);
            volume[kind] = static_cast<float>(sum);
        }
        // The volumes by the capacities' kinds; the sixth is a float of the
        // game's frame nothing sets (0 here).
        float byKind[6];
        byKind[0] = 0.0f;
        byKind[1] = static_cast<float>(static_cast<double>(volume[4]) + static_cast<double>(volume[1]));
        byKind[2] = static_cast<float>(static_cast<double>(volume[5]) + static_cast<double>(volume[3]));
        byKind[3] = volume[6];
        byKind[4] = volume[7];
        byKind[5] = 0.0f;
        for (int kind = 0; kind < 6; ++kind) {
            if (capacity[kind] == 0.0f)
                continue;
            Cdecl<void>(addr::cRZBaseString_Sprintf, &figures, " %s: cap(%.0f) vol(%.0f) cong(%.1f)",
                        kCapacityNames[kind], static_cast<double>(capacity[kind]), static_cast<double>(byKind[kind]),
                        static_cast<double>(byKind[kind]) / static_cast<double>(capacity[kind]));
            AppendString(&line.begin, figures, &allocator);
        }
        for (int kind = 0; kind < 9; ++kind) {
            if (volume[kind] == 0.0f)
                continue;
            Cdecl<void>(addr::cRZBaseString_Sprintf, &figures, " %s: %.0f", kVolumeNames[kind],
                        static_cast<double>(volume[kind]));
            AppendString(&line.begin, figures, &allocator);
        }
        AppendText(&line.begin, " ", &allocator);
        Release(network);
        Destroy(figures);
    }

    void* const lots = Global<void*>(addr::SC4_LotManager);
    if (Virtual<void*>(lots, 0x18, cellX, cellZ, uint32_t{0})) {
        void* const traffic = Global<void*>(addr::SC4_Service43DA8);
        const uint8_t length = Virtual<uint8_t>(Virtual<void*>(traffic, 0x34), 0x1C, cellX, cellZ);
        if (length != 0) {
            const double scale = Virtual<double>(traffic, 0x38);
            const float trip = static_cast<float>(scale * static_cast<double>(int32_t{length}) *
                                                  static_cast<double>(Global<float>(addr::SC4_FloatInv255)));
            GameString trips;
            MakeEmpty(trips);
            Cdecl<void>(addr::cRZBaseString_Sprintf, &trips, "Trip length(%.2f) ", static_cast<double>(trip));
            AppendString(&line.begin, trips, &allocator);
            Destroy(trips);
        } else {
            AppendText(&line.begin, "Trip length(n/a) ", &allocator);
        }
    }
    void* const lot = Virtual<void*>(Global<void*>(addr::SC4_LotManager), 0x18, cellX, cellZ, uint32_t{0});
    if (lot && !Virtual<void*>(lot, 0xEC)) {
        void* const traffic = Global<void*>(addr::SC4_Service43DA8);
        void* const holder = Virtual<void*>(lot, 0xC);
        const int32_t low = Virtual<int32_t>(traffic, 0x50, holder, uint32_t{0});
        const int32_t medium = Virtual<int32_t>(traffic, 0x50, holder, uint32_t{1});
        const int32_t high = Virtual<int32_t>(traffic, 0x50, holder, uint32_t{2});
        GameString jobs;
        MakeEmpty(jobs);
        Cdecl<void>(addr::cRZBaseString_Sprintf, &jobs, "Jobs %d:%d:%d", low, medium, high);
        AppendString(&line.begin, jobs, &allocator);
        Destroy(jobs);
    }
    AppendString(BodyOf(text), line, &allocator);
    if (powered.begin)
        Cdecl<void>(addr::SC4_Free, static_cast<void*>(powered.begin));
    if (zoneName.begin)
        Cdecl<void>(addr::SC4_Free, static_cast<void*>(zoneName.begin));
    if (grade.begin)
        Cdecl<void>(addr::SC4_Free, static_cast<void*>(grade.begin));
    Destroy(line);
}

// --- The developer shares of a lot -----------------------------------------------------------

// The map the lot fills (its slot 62): a red-black tree of developer type
// to share (a float at the node's +0x14), its header first, then the count.
struct ShareMap {
    char* header;
    uint32_t count;
};

// The game's 0x004CF2B0 (cdecl (cell x, cell z, text); callers 0x004D1D27,
// 0x004D4B35 and 0x004D5F0E): for the lot at the cell, each developer type
// of its shares as "%s %.0f%% (%.0f%%), ": its short name, the share and the
// demand at the cell (the developer's slot 13 map, 16 bits signed) times
// 0.01 held to 0 to 1 when the lot has room for the type (lot slot 52 with
// the developer's slot 6 and 0), else 0; then a line end. The map is
// emptied (0x004C56F0) and its header freed. Nothing without a lot.
void __cdecl AppendLotShares(int32_t x, int32_t z, void* text)
{
    void* const lot = Virtual<void*>(Global<void*>(addr::SC4_LotManager), 0x18, x, z, uint32_t{0});
    if (!lot)
        return;
    char* const header = Cdecl<char*>(addr::SC4_stl_allocate, uint32_t{0x18});
    ShareMap shares = {header, 0};
    header[0] = 0;
    At<char*>(header, 0x4) = nullptr;
    At<char*>(header, 0x8) = header;
    At<char*>(header, 0xC) = header;
    Virtual<void>(lot, 0xF8, &shares);
    uint8_t allocator = 0;
    char** const body = BodyOf(text);
    for (char* node = At<char*>(shares.header, 0x8); node != shares.header; node = TreeNext(node)) {
        const uint32_t type = At<uint32_t>(node, 0x10);
        const float share = At<float>(node, 0x14);
        GameString name;
        MakeEmpty(name);
        for (const auto& entry : kDeveloperNames) {
            if (entry.type != type)
                continue;
            if (entry.assign) {
                AssignWhole(&name, entry.name);
            } else {
                GameString made;
                This<void*>(addr::SC4String_FromText, &made, entry.name);
                This<void>(addr::SC4String_AssignRange, &name.begin, made.begin, made.end, &allocator);
                Destroy(made);
            }
            break;
        }
        void* const developer = Virtual<void*>(Global<void*>(addr::SC4_Service43D68), 0x80, type);
        const uint32_t room = Virtual<uint16_t>(lot, 0xD0, Virtual<uint32_t>(developer, 0x18), uint32_t{0});
        const int32_t demand = static_cast<int16_t>(Virtual<uint32_t>(Virtual<void*>(developer, 0x34), 0x1C, x, z));
        float level = 0.0f;
        if (static_cast<int32_t>(room) > 0) {
            level = static_cast<float>(static_cast<double>(demand) *
                                       static_cast<double>(Global<float>(addr::SC4Traffic_FerryCostScale)));
            if (level < 0.0f)
                level = 0.0f;
            else if (level > 1.0f)
                level = 1.0f;
        }
        char* const nameBegin = name.begin;
        GameString entryText;
        MakeEmpty(entryText);
        Cdecl<void>(addr::cRZBaseString_Sprintf, &entryText, "%s %.0f%% (%.0f%%), ", nameBegin,
                    static_cast<double>(share) * static_cast<double>(kHundred),
                    static_cast<double>(level) * static_cast<double>(kHundred));
        AppendString(body, entryText, &allocator);
        Destroy(entryText);
        name.vtable = addr::SC4String_Vtable;
        if (nameBegin)
            Cdecl<void>(addr::SC4_Free, static_cast<void*>(nameBegin));
    }
    AppendText(body, "\n", &allocator);
    if (shares.count) {
        This<void>(addr::SC4Uint32FloatMap_EraseSubtree, &shares, At<char*>(shares.header, 0x4));
        At<char*>(shares.header, 0x8) = shares.header;
        At<char*>(shares.header, 0x4) = nullptr;
        At<char*>(shares.header, 0xC) = shares.header;
        shares.count = 0;
    }
    if (shares.header)
        Cdecl<void>(addr::SC4_Free, static_cast<void*>(shares.header));
}

// --- Registration ----------------------------------------------------------------------

bool InstallQuery5b(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[8] = {};
    static const struct {
        std::uintptr_t address;
        void* ours;
    } hooks[] = {
        {addr::SC4QueryTool_AppendBuildingText, reinterpret_cast<void*>(&AppendBuildingText)},
        {addr::SC4QueryTool_AppendDemandText, reinterpret_cast<void*>(&AppendDemandText)},
        {addr::SC4QueryTool_OutcomeName, reinterpret_cast<void*>(&OutcomeNameThunk)},
        {addr::SC4QueryTool_LogDevelopmentFailures, reinterpret_cast<void*>(&LogDevelopmentFailures)},
        {addr::SC4QueryTool_LogDevelopmentAttempt, reinterpret_cast<void*>(&LogDevelopmentAttempt)},
        {addr::SC4QueryTool_AppendCellText, reinterpret_cast<void*>(&AppendCellText)},
        {addr::SC4QueryTool_AppendLotShares, reinterpret_cast<void*>(&AppendLotShares)},
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
    "ui.tools.query5b",
    "ui",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4QueryTool_AppendCellText,
    "Our query tool, fifth piece, second half: the debug texts, the development outcomes' names and the "
    "development log (docs/tools.md)",
    false,
    InstallQuery5b,
});

} // namespace
} // namespace opensc4::tool_query_5b
