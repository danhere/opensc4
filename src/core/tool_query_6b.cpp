// The query tool of the city view, sixth piece, second half: the cell log,
// 0x004D12A0 to 0x004D3179 (its jump tables follow to 0x004D3245).
// Replacement, off by default:
//
//   ui.tools.query6b: 1 function of the query tool
//     (cSC4ViewInputControlQuery: a view input control of 0xA0 bytes, vtable
//     0x00A90A88; docs/tools.md): the cell log 0x004D12A0, a debug dump of
//     everything the game knows about one cell, written to the file
//     "Query.txt" through the query's log lines (0x004C3750). The query
//     (0x004D4140) calls it only when its debug switch is on.
//
// The file is an STLport output file stream on the relative path
// "Query.txt" (the log object 0x0092CEF6, vtable 0x00AD19F8, 0x18 bytes on
// the frame): opened for writing, emptied (out, trunc, binary), flushed and
// closed before the function returns. Relative means the process's current
// directory: the game's Apps folder when started by our launcher. While it
// runs the log object is the query's log sink (0x00B21850); the sink is
// cleared at the end.
//
// Nothing here changes the city: every call only reads simulators, makes
// lists the network service fills and empties again, and writes strings
// and log lines. Nothing draws a random number or reads a clock (the
// desirability log 0x004C9520 it calls reads the simulator's day). The
// strings, lists and nodes are allocated and freed in the game's order.
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace opensc4::tool_query_6b {
namespace {

constexpr uint32_t kBuildingIid = 0x87DDA3A9;        // the building occupant's interface
constexpr uint32_t kNetworkOccupantIid = 0xA821EF94; // the network occupant's interface
constexpr uint32_t kConstructionIid = 0x496E7636;    // the construction occupant's interface
constexpr uint32_t kTypeConstruction = 0x096E763C;   // the construction occupants' type
constexpr uint32_t kTypeBuilding = 0x278128A0;       // the buildings' occupant type
constexpr uint32_t kTypeNetwork = 0xC772BF98;        // the network occupants' type

// The log lines that rule the file off.
constexpr const char kRule[] = "|-----------------------------------------------------------\n";
constexpr const char kRuleShort[] = "|----------------------------------------\n";
constexpr const char kBlank[] = "| \n";

// A lot's state (lot slot 33), condition (slot 35), facing (slot 23), the
// former occupants' wealth (slot 41) and why they left (slot 45).
constexpr const char* kLotStates[7] = {"created", "empty", "constructing", "occupied", "vacant", "destructing",
                                       "destroyed"};
constexpr const char* kLotConditions[7] = {" new",        " unpowered", " distressed1", " distressed2",
                                           " abandoned",  " scorched1", " scorched2"};
constexpr const char* kFacings[4] = {"north", "east", "south", "west"};
constexpr const char* kWealths[3] = {"low", "medium", "high"};
constexpr const char* kVacateReasons[6] = {"low desirability", "lack of power",  "lack of water",
                                           "terrible commute", "kicked out",     "negative demand"};

// The construction occupant's state (its +0x140).
constexpr const char* kConstructionStates[5] = {"Build Base", "Build Scaffold", "Construct Building",
                                                "Remove Scaffold", "Shutdown"};

// A building's wealth (its record's +0x4).
constexpr const char* kBuildingWealths[3] = {" $", " $$", " $$$"};

// The zone names, by zone type 0 to 15 (the zone manager's slot 12).
constexpr const char* kZoneNames[16] = {"None",     "R#",      "R##",     "R###",      "C#",      "C##",
                                        "C###",     "Ir",      "Il",      "Ih",        "Military", "Airport",
                                        "Seaport",  "Spaceport", "Landfill", "Plopped Bldg"};

// The land value's grade (the land value simulator's slot 5).
constexpr const char* kGrades[3] = {"Low", "Medium", "High"};

// The network kinds the log asks a network occupant about (its slot 22),
// with the destination kind given to 0x004C6A60 for each neighbour (0: the
// kind is skipped). The first seven names are made in line, the others by
// 0x00408480, as in the game.
constexpr uint32_t kNetworkKinds = 13;
constexpr uint32_t kInlineNames = 7;
constexpr const char* kNetworkNames[kNetworkKinds] = {
    "Road",     "Rail",       "Highway",  "Street",       "Pipe",      "Power Line",    "Avenue",
    "Subway",   "Light Rail", "Monorail", "One-Way Road", "Dirt Road", "Ground Highway",
};
constexpr int32_t kDestinationKinds[kNetworkKinds] = {2, 1, 3, 0, 5, 0, 8, 6, 7, 0xB, 0, 0xA, 0xC};

// The traffic simulator's travel types the edge densities are asked for.
constexpr int32_t kTravelTypes = 10;

// What the log writes for a zone's growth developers: their desirability
// equations (0x004C9520), the last successful build of any of them and the
// last failed build of each (the tract developer's records, 0x004CE440).
struct ZoneDevelopment {
    const uint32_t* types;
    uint32_t count;
    const char* const* equations;
    const char* success;
    const char* const* failures;
    bool copy; // the list goes to 0x00705E90 by 0x004C9430 (else by 0x006F1570 into a new head)
};

constexpr uint32_t kResidentialTypes[3] = {0x1010, 0x1020, 0x1030};
constexpr const char* kResidentialEquations[3] = {"| Desirability equation for R$\n", "| Desirability equation for R$$\n",
                                                  "| Desirability equation for R$$$\n"};
constexpr const char* kResidentialFailures[3] = {"| Last failed R$ build\n", "| Last failed R$$ build\n",
                                                 "| Last failed R$$$ build\n"};
constexpr uint32_t kCommercialTypes[5] = {0x3110, 0x3120, 0x3130, 0x3320, 0x3330};
constexpr const char* kCommercialEquations[5] = {
    "| Desirability equation for Cs$\n", "| Desirability equation for Cs$$\n",
    "| Desirability equation for Cs$$$\n", "| Desirability equation for Co$$\n",
    "| Desirability equation for Co$$$\n"};
constexpr const char* kCommercialFailures[5] = {"| Last failed Cs$ build\n", "| Last failed Cs$$ build\n",
                                                "| Last failed Cs$$$ build\n", "| Last failed Co$$ build\n",
                                                "| Last failed Co$$$ build\n"};
constexpr uint32_t kIndustrialTypes[4] = {0x4100, 0x4200, 0x4300, 0x4400};
constexpr const char* kIndustrialEquations[4] = {
    "| Desirability equation for I resource\n", "| Desirability equation for I dirty\n",
    "| Desirability equation for I manufacturing\n", "| Desirability equation for I high tech\n"};
constexpr const char* kIndustrialFailures[4] = {"| Last failed Ir build\n", "| Last failed Id build\n",
                                                "| Last failed Im build\n", "| Last failed Iht build\n"};

constexpr ZoneDevelopment kZones[3] = {
    {kResidentialTypes, 3, kResidentialEquations, "| Last successful R build\n", kResidentialFailures, false},
    {kCommercialTypes, 5, kCommercialEquations, "| Last successful C build\n", kCommercialFailures, false},
    {kIndustrialTypes, 4, kIndustrialEquations, "| Last successful I build\n", kIndustrialFailures, true},
};

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

// The game's inline end of a string: the buffer freed (the vtable set again
// where the game does it, which changes nothing).
void FreeBody(GameString& text)
{
    if (text.begin)
        Cdecl<void>(addr::SC4_Free, static_cast<void*>(text.begin));
}

void Destroy(GameString& text)
{
    text.vtable = addr::SC4String_Vtable;
    FreeBody(text);
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

// A string of a C string the game builds in line: a buffer of the length
// plus one from the STL allocator, the characters copied, a zero after.
void MakeInline(GameString& text, const char* from)
{
    const std::size_t length = std::strlen(from);
    char* const buffer = Cdecl<char*>(addr::SC4_stl_allocate, static_cast<uint32_t>(length + 1));
    text.begin = buffer;
    text.capacity = buffer + length + 1;
    std::memcpy(buffer, from, length);
    text.end = buffer + length;
    *text.end = 0;
    text.vtable = addr::SC4String_Vtable;
    text.zero = 0;
}

// A C string through a string made for it (0x00408480), assigned with
// Assign (0x00424380); the made string is then destroyed by
// SC4String::Destroy (0x00414250) or, where the game does it in line, only
// its buffer freed.
void AssignDestroyed(GameString& to, const char* text)
{
    GameString made;
    This<void*>(addr::SC4String_FromText, &made, text);
    This<void>(addr::cRZBaseString_Assign, &to, &made);
    This<void>(addr::SC4String_Destroy, &made);
}

void AssignFreed(GameString& to, const char* text)
{
    GameString made;
    This<void*>(addr::SC4String_FromText, &made, text);
    This<void>(addr::cRZBaseString_Assign, &to, &made);
    FreeBody(made);
}

// A C string's characters as a string's body (0x00408A70) through a string
// made for it, whose buffer is freed after.
void AssignBody(GameString& to, const char* text, void* allocator)
{
    GameString made;
    This<void*>(addr::SC4String_FromText, &made, text);
    This<void>(addr::SC4String_AssignRange, &to.begin, made.begin, made.end, allocator);
    FreeBody(made);
}

// A C string appended to a string's body (0x00410F00).
void AppendText(GameString& to, const char* text)
{
    This<void*>(addr::cSC4Advisor_F410F00, &to.begin, text);
}

// --- Helpers by address -----------------------------------------------------------------

// The game's 0x004C6A60 takes the cell x in EBX and z in EDI and one stack
// argument the caller removes: (x, z, destination kind, function).
__declspec(naked) void DestinationThunk()
{
    __asm push ebx
    __asm push edi
    __asm mov ebx, dword ptr [esp + 12]
    __asm mov edi, dword ptr [esp + 16]
    __asm push dword ptr [esp + 20]
    __asm call dword ptr [esp + 28]
    __asm add esp, 4
    __asm pop edi
    __asm pop ebx
    __asm ret
}

// The name of what the month-end service has at a neighbour cell, logged
// (0x004C6A60, the game's).
void LogDestination(int32_t x, int32_t z, int32_t kind)
{
    using ThunkFn = void(__cdecl*)(int32_t, int32_t, int32_t, std::uintptr_t);
    reinterpret_cast<ThunkFn>(&DestinationThunk)(x, z, kind, addr::SC4QueryTool_LogDestination);
}

// A list the game makes in line for a list argument: a sentinel and nodes
// of 0xC bytes from the STL allocator, each value pushed at the back.
char* MakeTypeList(const uint32_t* types, uint32_t count)
{
    char* const sentinel = Cdecl<char*>(addr::SC4_stl_allocate, uint32_t{0xC});
    At<char*>(sentinel, 0x0) = sentinel;
    At<char*>(sentinel, 0x4) = sentinel;
    for (uint32_t i = 0; i < count; ++i) {
        char* const node = Cdecl<char*>(addr::SC4_stl_allocate, uint32_t{0xC});
        At<uint32_t>(node, 0x8) = types[i];
        char* const last = At<char*>(sentinel, 0x4);
        At<char*>(node, 0x0) = sentinel;
        At<char*>(node, 0x4) = last;
        At<char*>(last, 0x0) = node;
        At<char*>(sentinel, 0x4) = node;
    }
    return sentinel;
}

// --- The parts of the log ----------------------------------------------------------------

// The lot's lines: position, size, facing, state and configuration; for a
// vacant lot the former occupants, for one under construction the
// construction occupant's state; then its jobs. `box` gets the lot's
// bounding rectangle (lot slot 14).
void LogLot(void* lot, int32_t x, int32_t z, int32_t* box, uint8_t* allocator)
{
    int32_t lotX = 0, lotZ = 0;
    Virtual<void>(lot, 0x54, &lotX, &lotZ);
    uint8_t sizeZ = 0;
    Virtual<void>(lot, 0x58, allocator, &sizeZ);
    int32_t rect[4] = {};
    Virtual<void>(lot, 0x38, rect);
    box[0] = rect[0];
    box[1] = rect[1];
    box[2] = rect[2];
    box[3] = rect[3];

    GameString state;
    MakeEmpty(state);
    const uint32_t lotState = Virtual<uint32_t>(lot, 0x84);
    bool condition = false;
    if (lotState <= 6) {
        AssignDestroyed(state, kLotStates[lotState]);
        condition = lotState == 3 || lotState == 4;
    }
    if (condition) {
        const uint32_t which = Virtual<uint32_t>(lot, 0x8C);
        if (which <= 6)
            AppendText(state, kLotConditions[which]);
    }

    GameString facing;
    MakeEmpty(facing);
    const uint32_t direction = Virtual<uint32_t>(lot, 0x5C);
    if (direction <= 3)
        AssignFreed(facing, kFacings[direction]);

    GameString configuration;
    This<void*>(addr::SC4String_FromText, &configuration, "none");
    if (void* const config = Virtual<void*>(lot, 0x24)) {
        GameString name;
        MakeEmpty(name);
        Virtual<void>(config, 0x10, &name);
        char* const nameBegin = name.begin;
        const uint32_t id = Virtual<uint32_t>(config, 0xC);
        Cdecl<void>(addr::cRZBaseString_Sprintf, &configuration, "%s_%08x", nameBegin, id);
        Destroy(name);
    }
    Log("| Lot: (%d, %d) %dx%d, %s-facing, state: %s, configuration: %s\n", lotX, lotZ, uint32_t{*allocator},
        uint32_t{sizeZ}, facing.begin, state.begin, configuration.begin);

    if (lotState == 4) {
        GameString wealth;
        This<void*>(addr::SC4String_FromText, &wealth, "unknown");
        const int32_t level = Virtual<int32_t>(lot, 0xA4);
        if (level >= 1 && level <= 3)
            AssignFreed(wealth, kWealths[level - 1]);
        GameString reason;
        This<void*>(addr::SC4String_FromText, &reason, "unknown cause");
        const uint32_t why = Virtual<uint32_t>(lot, 0xB4) - 1;
        if (why <= 5)
            AssignDestroyed(reason, kVacateReasons[why]);
        Log("| Former occupants were %s wealth and vacated due to %s\n", wealth.begin, reason.begin);
        FreeBody(reason);
        FreeBody(wealth);
    } else if (lotState == 2) {
        void* occupant = nullptr;
        Virtual<uint8_t>(Global<void*>(addr::SC4_NetworkService), 0x84, &occupant, x, z, kTypeConstruction);
        if (occupant) {
            char* construction = nullptr;
            if (Virtual<uint8_t>(occupant, 0x0, kConstructionIid, &construction)) {
                GameString text;
                This<void*>(addr::SC4String_FromText, &text, "error");
                const uint32_t step = At<uint32_t>(construction, 0x140);
                if (step <= 4)
                    AssignDestroyed(text, kConstructionStates[step]);
                Log("| Construction occupant state: %s\n", text.begin);
                FreeBody(text);
            }
            Release(occupant);
            if (construction)
                Release(construction);
        } else {
            Log("| Construction occupant not found\n");
        }
    }

    // The jobs by wealth (lot slot 64: three floats from +0x4) and either
    // where its travel designate lies (slot 59, its slot 21) or the traffic
    // simulator's maximum trips of the lot's holder (slot 20 by wealth).
    void* const traffic = Global<void*>(addr::SC4_Service43DA8);
    void* const holder = Virtual<void*>(lot, 0xC);
    const int32_t low = Virtual<int32_t>(traffic, 0x50, holder, uint32_t{0});
    int32_t medium = Virtual<int32_t>(traffic, 0x50, holder, uint32_t{1});
    int32_t high = Virtual<int32_t>(traffic, 0x50, holder, uint32_t{2});
    // Slot 64 fills four dwords; the frame has 0x10 bytes there, ours more
    // in case the service it asks writes further.
    float jobs[8] = {};
    Virtual<void>(lot, 0x100, jobs);
    if (void* const designate = Virtual<void*>(lot, 0xEC)) {
        Virtual<void>(designate, 0x54, &medium, &high);
        Log("| Jobs $%.1f $$%.1f $$$%.1f -> (%d, %d)\n", static_cast<double>(jobs[1]), static_cast<double>(jobs[2]),
            static_cast<double>(jobs[3]), medium, high);
    } else {
        Log("| Jobs $%.1f $$%.1f $$$%.1f - Travel Jobs $%d $$%d $$$%d\n", static_cast<double>(jobs[1]),
            static_cast<double>(jobs[2]), static_cast<double>(jobs[3]), low, medium, high);
    }

    Destroy(configuration);
    FreeBody(facing);
    Destroy(state);
}

// The first building in the box (the list the network service filled) with
// its capacity and wealth, and the developer shares of the lot at the cell
// (0x004CF2B0).
void LogBuilding(char* buildings, int32_t x, int32_t z, uint8_t* allocator)
{
    char* const first = At<char*>(buildings, 0x0);
    if (first == buildings)
        return;
    void* const occupant = At<void*>(first, 0x8);
    if (!occupant)
        return;
    void* building = nullptr;
    if (Virtual<uint8_t>(occupant, 0x0, kBuildingIid, &building)) {
        GameString name;
        MakeEmpty(name);
        void* const source = Virtual<void*>(building, 0x34);
        // The string's slot 13 (operator=), called through the vtable's word.
        This<void*>(Global<std::uintptr_t>(addr::SC4String_Vtable + 0x34), &name, source);
        char* const record = Virtual<char*>(building, 0x2C);
        if (At<uint16_t>(record, 0x8) != 0) {
            GameString capacity;
            This<void*>(addr::SC4String_SC4String, &capacity);
            Cdecl<void>(addr::cRZBaseString_Sprintf, &capacity, ", %d", uint32_t{At<uint16_t>(record, 0x8)});
            This<void>(addr::SC4String_AppendRange, &name.begin, capacity.begin, capacity.end,
                       static_cast<void*>(allocator));
            Destroy(capacity);
        }
        const uint32_t wealth = At<uint32_t>(record, 0x4) - 1;
        if (wealth <= 2)
            AppendText(name, kBuildingWealths[wealth]);
        Log("| Building: %s\n", name.begin);
        Release(building);
        Destroy(name);
    }
    GameString shares;
    MakeEmpty(shares);
    Cdecl<void>(addr::SC4QueryTool_AppendLotShares, x, z, static_cast<void*>(&shares));
    if (shares.begin != shares.end)
        Log("| Occupancy--building (tract): %s", shares.begin);
    FreeBody(shares);
}

// Each network occupant at the cell: its kinds and, for each direction it
// connects in, what lies on the neighbour cell (0x004C6A60).
void LogNetworks(char* networks, int32_t x, int32_t z)
{
    for (char* node = At<char*>(networks, 0x0); node != networks; node = At<char*>(node, 0x0)) {
        void* const occupant = At<void*>(node, 0x8);
        Log("| Network: ");
        void* piece = nullptr;
        if (!Virtual<uint8_t>(occupant, 0x0, kNetworkOccupantIid, &piece))
            continue;
        GameString names[kNetworkKinds];
        for (uint32_t i = 0; i < kInlineNames; ++i)
            MakeInline(names[i], kNetworkNames[i]);
        for (uint32_t i = kInlineNames; i < kNetworkKinds; ++i)
            This<void*>(addr::SC4String_FromText, &names[i], kNetworkNames[i]);
        for (uint32_t i = 0; i < kNetworkKinds; ++i) {
            if (kDestinationKinds[i] == 0)
                continue;
            if (!Virtual<uint8_t>(piece, 0x58, i))
                continue;
            Log("%s ", names[i].begin);
            if (Virtual<uint8_t>(piece, 0xBC, uint32_t{0}, i)) {
                Log("W ");
                LogDestination(x - 1, z, kDestinationKinds[i]);
            }
            if (Virtual<uint8_t>(piece, 0xBC, uint32_t{1}, i)) {
                Log("N ");
                LogDestination(x, z - 1, kDestinationKinds[i]);
            }
            if (Virtual<uint8_t>(piece, 0xBC, uint32_t{2}, i)) {
                Log("E ");
                LogDestination(x + 1, z, kDestinationKinds[i]);
            }
            if (Virtual<uint8_t>(piece, 0xBC, uint32_t{3}, i)) {
                Log("S ");
                LogDestination(x, z + 1, kDestinationKinds[i]);
            }
        }
        Log("\n");
        Release(piece);
        for (uint32_t i = kNetworkKinds; i-- > 0;)
            Destroy(names[i]);
    }
}

// The traffic at the cell: volume and congestion (the traffic simulator's
// slot 9 and slot 10 maps), then for each travel type the edge density
// matrix (slot 12: a record of 4 by 4 counts, 16 bits, and their total at
// +0x20) one way and, when the total is not 0, the other.
void LogTraffic(int32_t x, int32_t z)
{
    void* const traffic = Global<void*>(addr::SC4_Service43DA8);
    void* const congestionMap = Virtual<void*>(traffic, 0x28);
    void* const volumeMap = Virtual<void*>(traffic, 0x24);
    const uint32_t congestion = Virtual<uint8_t>(congestionMap, 0x1C, x, z);
    const uint32_t volume = Virtual<uint8_t>(volumeMap, 0x1C, x, z);
    Log("| Traffic Volume: %d   Traffic Congestion: %d\n", volume, congestion);
    for (int32_t type = 0; type < kTravelTypes; ++type) {
        char* const out = Virtual<char*>(traffic, 0x30, x, z, type, uint32_t{0});
        const uint32_t total = At<uint16_t>(out, 0x20);
        Log("| Edge Density Matrix for travel type %d (total = %d):\n", type, total);
        if (total == 0)
            continue;
        char* const back = Virtual<char*>(traffic, 0x30, x, z, type, uint32_t{1});
        for (std::size_t row = 0; row < 4; ++row) {
            const std::size_t at = row * 8;
            Log("| %3d %3d %3d %3d     %3d %3d %3d %3d\n", uint32_t{At<uint16_t>(out, at)},
                uint32_t{At<uint16_t>(out, at + 2)}, uint32_t{At<uint16_t>(out, at + 4)},
                uint32_t{At<uint16_t>(out, at + 6)}, uint32_t{At<uint16_t>(back, at)},
                uint32_t{At<uint16_t>(back, at + 2)}, uint32_t{At<uint16_t>(back, at + 4)},
                uint32_t{At<uint16_t>(back, at + 6)});
        }
    }
}

// A zone's growth developers: the desirability equations, the last
// successful build of the zone's types (the tract developer's 0x00705E90
// with a list of them, which the callee takes by value and frees) and the
// last failed build of each type (0x00705FA0).
void LogDevelopment(const ZoneDevelopment& zone, void* developer, int32_t x, int32_t z, uint8_t* allocator)
{
    for (uint32_t i = 0; i < zone.count; ++i) {
        Log(kBlank);
        Log(zone.equations[i]);
        Cdecl<void>(addr::SC4QueryTool_LogDesirability, x, z, zone.types[i]);
        Log(kBlank);
        Log(i + 1 < zone.count ? kRuleShort : kRule);
    }
    Log(kBlank);
    Log(zone.success);
    char* local = MakeTypeList(zone.types, zone.count);
    void* argument = nullptr;
    if (zone.copy) {
        This<void>(addr::SC4StdList_CopyConstructUint32, &argument, &local);
    } else {
        char* const head = Cdecl<char*>(addr::SC4_stl_allocate, uint32_t{0xC});
        At<char*>(head, 0x0) = head;
        At<char*>(head, 0x4) = head;
        argument = head;
        This<void>(addr::SC4StdList_InsertRangeUint32, &argument, At<char*>(head, 0x0), At<char*>(local, 0x0), local,
                   static_cast<void*>(allocator));
    }
    void* const last = This<void*>(addr::cSC4TractDeveloper_Fn_00705E90, developer, x, z, argument);
    Cdecl<void>(addr::SC4QueryTool_LogDevelopmentAttempt, last);
    Log(kBlank);
    Log(kRule);
    for (uint32_t i = 0; i < zone.count; ++i) {
        Log(kBlank);
        Log(zone.failures[i]);
        void* const failed = This<void*>(addr::cSC4TractDeveloper_Fn_00705FA0, developer, x, z, zone.types[i]);
        Cdecl<void>(addr::SC4QueryTool_LogDevelopmentAttempt, failed);
        Log(kBlank);
        Log(i + 1 < zone.count ? kRuleShort : kRule);
    }
    This<void>(addr::SC4PairList_Destroy, &local);
}

// --- The cell log --------------------------------------------------------------------------

// The game's 0x004D12A0 (cdecl (cell x, cell z); callers 0x004D4825 and
// 0x004D4935 in 0x004D4140): opens "Query.txt" (log object 0x0092CEF6 with
// 1, 0, 0, 0: emptied), makes it the log sink and writes: the cell and the
// sim date (the simulator service's slot 7, its slot 41); the lot at the
// cell (lot manager slot 6) if any; the first building in the lot's box (or
// the cell: network service slot 41 for type 0x278128A0) and the lot's
// developer shares; the zone (zone manager slot 12), the altitude (terrain
// slot 13 at the cell's centre, city slot 97), the land value (0x00B43D88
// slots 7, 6, 5), the slope (lot slot 18, or the land value simulator's
// slot 9 map), powered (power slot 19), watered (0x00B43D94 slot 11), a
// landfill's use (0x00B43D28 slot 10; the ratio not times 100, as in the
// game); the network occupants at the cell (network service slot 41 for
// 0xC772BF98); the traffic; for zones 1 to 9 the zone's development. Then
// the file is flushed, the sink cleared and everything let go.
void __cdecl LogCell(int32_t x, int32_t z)
{
    alignas(4) uint8_t logFile[0x18];
    This<void*>(addr::SC4FileLog_Construct, static_cast<void*>(logFile), "Query.txt", uint32_t{1}, uint32_t{0},
                uint32_t{0}, uint32_t{0});
    Global<void*>(addr::SC4QueryTool_LogSink) = logFile;

    float centre[2] = {};
    Virtual<void>(Global<void*>(addr::SC4_City), 0x184, x, z, &centre[0], &centre[1]);
    void* const developer = Virtual<void*>(Global<void*>(addr::SC4_Service43D68), 0x8C);
    GameString date;
    MakeEmpty(date);
    void* const clock = Virtual<void*>(Global<void*>(addr::SC4_SimulatorService), 0x1C);
    Virtual<void>(clock, 0xA4, &date, uint32_t{0});
    Log("\n");
    Log(kRule);
    Log("| Query info for cell (%d, %d) on %s\n", x, z, date.begin);
    Log(kRule);

    // The byte the game passes as the STL allocator argument doubles as the
    // lot's first size (lot slot 22 writes it).
    uint8_t allocator = 0;
    int32_t box[4] = {x, z, x, z};
    void* const lot = Virtual<void*>(Global<void*>(addr::SC4_LotManager), 0x18, x, z, uint32_t{0});
    if (lot)
        LogLot(lot, x, z, box, &allocator);

    char* const buildings = static_cast<char*>(Virtual<void*>(ListNodeAllocator(), 0xC, uint32_t{0xC}));
    At<char*>(buildings, 0x0) = buildings;
    At<char*>(buildings, 0x4) = buildings;
    void* buildingList = buildings;
    Virtual<void>(Global<void*>(addr::SC4_NetworkService), 0xA4, &buildingList, &box[0], &box[2], kTypeBuilding,
                  uint32_t{1});
    LogBuilding(static_cast<char*>(buildingList), x, z, &allocator);
    Virtual<void>(Global<void*>(addr::SC4_NetworkService), 0xC0, &buildingList);

    const uint32_t zone = Virtual<uint32_t>(Global<void*>(addr::SC4_Service43D14), 0x30, x, z);
    static const char kUnknown[] = "Unknown";
    GameString zoneName;
    zoneName.begin = nullptr;
    zoneName.end = nullptr;
    zoneName.capacity = nullptr;
    This<void>(addr::SC4String_FromRange, &zoneName.begin, &kUnknown[0], &kUnknown[7],
               static_cast<void*>(&allocator));
    zoneName.vtable = addr::SC4String_Vtable;
    zoneName.zero = 0;
    if (zone <= 15)
        AssignDestroyed(zoneName, kZoneNames[zone]);
    Log("| Zoned: %s\n", zoneName.begin);
    Log("| Altitude: %.1f\n", Virtual<double>(Global<void*>(addr::SC4_Terrain), 0x34, centre[0], centre[1]));

    void* const landValue = Global<void*>(addr::SC4_Service43D88);
    const uint32_t intrinsic = Virtual<uint8_t>(Virtual<void*>(landValue, 0x1C), 0x1C, x, z);
    const uint32_t total = Virtual<uint8_t>(landValue, 0x18, x, z);
    const int32_t grade = Virtual<int32_t>(landValue, 0x14, x, z);
    GameString gradeName;
    MakeEmpty(gradeName);
    if (grade >= 1 && grade <= 3)
        AssignBody(gradeName, kGrades[grade - 1], &allocator);
    Log("| Land value, intrinsic: %d, total: %d (%s)\n", intrinsic, total, gradeName.begin);
    const double slope =
        lot ? Virtual<double>(lot, 0x48) : Virtual<double>(Virtual<void*>(landValue, 0x24), 0x1C, x, z);
    Log("| Slope: %.1f\n", slope);
    Log("| Powered: %s\n", Virtual<uint8_t>(Global<void*>(addr::SC4_Service43DA0), 0x4C, x, z) ? "yes" : "no");
    Log("| Watered: %s\n", Virtual<uint8_t>(Global<void*>(addr::SC4_Service43D94), 0x2C, x, z) ? "yes" : "no");
    if (zone == 14) {
        uint32_t used = 0, capacity = 0;
        Virtual<void>(Global<void*>(addr::SC4_Value43D28), 0x28, &used, &capacity);
        Log("| Landfill use: %d / %d = %.0f%%\n", used, capacity,
            static_cast<double>(used) / static_cast<double>(capacity));
    }

    int32_t cell[2] = {x, z};
    int32_t cellAgain[2] = {x, z};
    char* const networks = static_cast<char*>(Virtual<void*>(ListNodeAllocator(), 0xC, uint32_t{0xC}));
    At<char*>(networks, 0x0) = networks;
    At<char*>(networks, 0x4) = networks;
    void* networkList = networks;
    Virtual<void>(Global<void*>(addr::SC4_NetworkService), 0xA4, &networkList, cellAgain, cell, kTypeNetwork,
                  uint32_t{0xFFFFFFFF});
    LogNetworks(static_cast<char*>(networkList), x, z);
    Virtual<void>(Global<void*>(addr::SC4_NetworkService), 0xC0, &networkList);
    LogTraffic(x, z);
    Log(kRule);

    if (zone >= 1 && zone <= 9)
        LogDevelopment(kZones[(zone - 1) / 3], developer, x, z, &allocator);

    This<void>(addr::SC4FileLog_Flush, static_cast<void*>(logFile));
    Global<void*>(addr::SC4QueryTool_LogSink) = nullptr;
    This<void>(addr::SC4List_Destroy2, &networkList);
    FreeBody(gradeName);
    FreeBody(zoneName);
    This<void>(addr::SC4List_Destroy2, &buildingList);
    Destroy(date);
    This<void>(addr::SC4FileLog_Destroy, static_cast<void*>(logFile));
}

// --- Registration ----------------------------------------------------------------------

bool InstallQuery6b(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[1] = {};
    static const struct {
        std::uintptr_t address;
        void* ours;
    } hooks[] = {
        {addr::SC4QueryTool_LogCell, reinterpret_cast<void*>(&LogCell)},
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
    "ui.tools.query6b",
    "ui",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4QueryTool_LogCell,
    "Our query tool, sixth piece, second half: the cell log to Query.txt (docs/tools.md)",
    false,
    InstallQuery6b,
});

} // namespace
} // namespace opensc4::tool_query_6b
