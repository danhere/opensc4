// The query tool's part of the autotest, first piece (src/core/
// autotest_hooks.h; tool_query_1a.cpp, tool_query_1b.cpp): calls the
// functions of 0x004C3730 to 0x004C56F0 and hashes what comes out, so that a
// run with ui.tools.query1a and ui.tools.query1b on and one with them off
// can be compared. Only the game's addresses and vtable slots are called,
// never our own functions.
//
//   OPENSC4_TEST_Q1=<day>    token "query1", sub-hashes on the log lines
//     "autotest: query1: ...". Everything here leaves the city as it was.
//     OPENSC4_TEST_Q1_PARTS picks the parts (default all, 255):
//
//   1   pure: 0x004C40B0 for -2 to 14, 0x004C4100 for 0 to 4, 0x004C3B10
//       over a grid of numbers (the result as a float's bits), 0x004C4260
//       for seven levels (the text).
//   2   filters: the five occupant filters made as the game makes them (0xC
//       bytes, the base constructor, the two vtables); slot 3 over a fixed
//       sample of the city's occupants, slot 4 over ten type IDs.
//   4   lots: for up to 24 buildings 0x004C4160, 0x004C53A0, 0x004C4210,
//       0x004C5490 with the building's occupant, and the five map levels
//       (0x004C4750 to 0x004C4A30) at the lot's first cell with the object
//       0x004D32D0 gives for the lot.
//   8   fillers: a stand-in window keeps the value a filler stores. For the
//       same buildings the table's reader 0x004D3A90 with fill 1 for eleven
//       of the twelve table entries of this piece (the historical check box
//       is only read), then 0x004C3D70 and 0x004C5280 at the lot's first
//       cell and 0x004C3E10. OPENSC4_TEST_Q1_FILLERS is a mask over the
//       twelve (bit 0 the table's index 1, ..., bit 11 index 122). Filler 1
//       (0x004C5180) is left to the commit: the service it asks (0x00B43D78,
//       slot 10) has the game work out the road access of the building's
//       lot and keep it in the lot (the map at +0x40, not saved), in the
//       original too.
//   16  window helpers on the stand-in: 0x004C3B90, 0x004C3C00, 0x004C3C50
//       with and without a window, the two walks 0x004C4340 and 0x004C4370,
//       0x004C3CA0 twice (a value made, then found), and 0x004C3750 with
//       and without a stand-in at 0x00B21850.
//   32  object: the constructor on game memory for both kinds (the fields),
//       0x004C3810 for a sample of occupants (the name), 0x004C3860 and
//       0x004C4AE0 on a hand-made trip record with two routes in the three
//       modes, 0x004C4630 without panels; the last reference. Then the tool
//       as the view makes it for 0x6A935CF3 and 0x6A935CF4: its fields
//       after Init, OnMouseUpL once, the view puts it away; all in this
//       tick, so its timer cannot come.
//   64  picks: 0x004C4C90 with an askable filter over a grid of screen
//       points (found, the occupant's type); 0x004C4D10 and 0x004C4DC0 over
//       the same grid.
//   128 small classes: the panel target's deleting destructor; the window
//       runner made on a counting stand-in, its QueryInterface for four
//       IDs, slot 6 with an object and with null. 0x004C5690 is not called
//       (it ends a modal run).
//
//   OPENSC4_TEST_Q1_COMMIT=1 adds filler 1 for the 24 buildings (their lots
//   keep their road access from then on) and the one function that changes
//   the city: 0x004C55A0 with fill 0 on the first building's lot, the
//   stand-in's check box saying the opposite of what the lot is; the lot
//   read back (its slot 43 and the filler with fill 1); then the same call
//   with the box saying what the lot was, so the lot's flag ends as it
//   began.
//
// Kept out of the hashes: every pointer (only whether it is set), what the
// constructor leaves as it comes, the timer's insides, whether the debug
// keys are down (0x004C4700) and the sound (0x004C4F40 is called for three
// occupants, nothing of it hashed); the frame-driven picks of automata are
// on a "varies" log line.
#include "core/autotest_hooks.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/log.h"
#include "core/tool_query.h"

#include <opensc4/addresses.h>

#include <GZServPtrs.h>
#include <cIGZWin.h>
#include <cISC4App.h>
#include <cISC4City.h>
#include <cISC4View3DWin.h>
#include <cRZAutoRefCount.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <string>
#include <vector>

namespace opensc4::autotest {
namespace {

struct Fnv {
    uint32_t h = 2166136261u;
    void Bytes(const void* data, std::size_t size)
    {
        for (std::size_t i = 0; i < size; ++i)
            h = (h ^ static_cast<const uint8_t*>(data)[i]) * 16777619u;
    }
    template <typename T>
    void Add(const T& value)
    {
        Bytes(&value, sizeof value);
    }
};

char g_token[48];

namespace base = tool_query::base;
namespace field = tool_query::field;
namespace runner = tool_query::window_runner;

constexpr uint32_t kValueControl = 0xC12CEA13, kValueProperty = 0xEA563AB0;
constexpr uint32_t kTextEdit = 0x231A1C57, kTextControl = 0x212CDC1F, kNumberControl = 0x21325207;
constexpr uint32_t kCheckControl = 0x8810;
constexpr int32_t kOff = -1000; // a screen point that picks nothing
constexpr uint32_t kFillerIndexes[] = {1, 6, 12, 21, 41, 45, 52, 58, 65, 94, 109, 122};
constexpr uint32_t kHistorical = 0x1000 | 122;

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

uint32_t FloatBits(float value)
{
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof bits);
    return bits;
}

// The city view, as tool_bulldoze_autotest.cpp finds it.
char* CityView()
{
    constexpr uint32_t kWinSC4App = 0x6104489A;
    constexpr uint32_t kWinView3D = 0x9A47B417;
    cISC4AppPtr app;
    cIGZWin* const main = app ? app->GetMainWindow() : nullptr;
    cIGZWin* const parent = main ? main->GetChildWindowFromID(kWinSC4App) : nullptr;
    cRZAutoRefCount<cISC4View3DWin> view;
    if (parent)
        parent->GetChildAs(kWinView3D, kGZIID_cISC4View3DWin, view.AsPPVoid());
    return reinterpret_cast<char*>(static_cast<cISC4View3DWin*>(view));
}

// A game string (0x14 bytes) made and freed as the game does.
struct OwnString {
    char bytes[0x14];
    OwnString() { This<void*>(addr::SC4String_SC4String, &bytes[0]); }
    explicit OwnString(const char* text) { This<void*>(addr::SC4String_FromText, &bytes[0], text); }
    ~OwnString()
    {
        if (void* const buffer = At<void*>(&bytes[0], 0x4))
            Cdecl<void>(addr::SC4_Free, buffer);
    }
    OwnString(const OwnString&) = delete;
    OwnString& operator=(const OwnString&) = delete;
    void* Pointer() { return &bytes[0]; }
    std::string Text()
    {
        const char* const begin = At<const char*>(&bytes[0], 0x4);
        const char* const end = At<const char*>(&bytes[0], 0x8);
        return begin && end >= begin ? std::string(begin, end) : std::string("?");
    }
};

// --- The city's occupants ------------------------------------------------------------------

// A fixed sample, as occfilters_autotest.cpp takes it: the first occupant of
// eight types in every eighth cell and the buildings of the lots there, up
// to 120; and up to 24 lots with a building, with the building's occupant.
struct Building {
    void* lot;
    void* occupant;
};

struct Sample {
    std::vector<void*> chosen;
    std::vector<void*> held;
    std::vector<Building> buildings;
    ~Sample()
    {
        for (void* const one : held)
            Virtual<uint32_t>(one, 0x8);
    }
};

void Collect(Sample& out, int32_t sizeX, int32_t sizeZ)
{
    void* const manager = Global<void*>(addr::SC4_NetworkService);
    void* const lotManager = Global<void*>(addr::SC4_LotManager);
    if (!manager || !lotManager)
        return;
    std::vector<void*> all;
    const auto add = [&all](void* occupant) {
        if (occupant && std::find(all.begin(), all.end(), occupant) == all.end())
            all.push_back(occupant);
    };
    constexpr uint32_t kTypes[] = {0xA823821E, 0x74758926, 0x497F6D9D, 0xC772BF98,
                                   0x68FD0C69, 0x096E763C, 0x895D1169, 0x69D25C5F};
    for (int32_t z = 0; z < sizeZ && all.size() < 6000; z += 8) {
        for (int32_t x = 0; x < sizeX && all.size() < 6000; x += 8) {
            for (const uint32_t type : kTypes) {
                void* found = nullptr;
                if (Virtual<uint8_t>(manager, 0x84, &found, x, z, type) && found) {
                    out.held.push_back(found);
                    add(found);
                }
            }
            if (void* const lot = Virtual<void*>(lotManager, 0x18, x, z, uint32_t{0})) {
                if (void* const building = Virtual<void*>(lot, 0xC0))
                    add(Virtual<void*>(building, 0xC));
            }
        }
    }
    constexpr std::size_t kMost = 120;
    const std::size_t count = all.size();
    const std::size_t take = count < kMost ? count : kMost;
    for (std::size_t i = 0; i < take; ++i)
        out.chosen.push_back(all[i * count / take]);
    for (int32_t z = 0; z < sizeZ && out.buildings.size() < 24; z += 3) {
        for (int32_t x = 0; x < sizeX && out.buildings.size() < 24; x += 3) {
            void* const lot = Virtual<void*>(lotManager, 0x18, x, z, uint32_t{0});
            void* const building = lot ? Virtual<void*>(lot, 0xC0) : nullptr;
            void* const occupant = building ? Virtual<void*>(building, 0xC) : nullptr;
            if (!occupant)
                continue;
            if (std::none_of(out.buildings.begin(), out.buildings.end(),
                             [lot](const Building& b) { return b.lot == lot; }))
                out.buildings.push_back({lot, occupant});
        }
    }
}

// --- The stand-ins ---------------------------------------------------------------------------

// A counted object with a vtable of ours. One kind stands for a window, a
// control that shows a value, a text edit, a check box and a window
// property; a text control and a number control have their slot 4 with
// another argument count and so vtables of their own.
struct Fake {
    void** vtable;
    int32_t references;
};

struct Property {
    uint32_t id;
    void* object;
};

void* g_windowVtable[128];
void* g_textVtable[8];
void* g_numberVtable[8];
void* g_sinkVtable[16];
Fake g_window, g_text, g_number, g_sink, g_object, g_other;
Fnv g_events;               // what the stand-ins were asked, in order
Property g_properties[8];   // the stand-in window's properties
bool g_children = true;     // does the stand-in window have children
uint8_t g_checked = 0;      // what the stand-in check box says
int32_t g_checks[2];        // times the check box was cleared, set
std::string g_sinkText;
const void* g_expectText = nullptr;

uint32_t __fastcall FakeAddRef(Fake* self, void*)
{
    return static_cast<uint32_t>(++self->references);
}

uint32_t __fastcall FakeRelease(Fake* self, void*)
{
    return static_cast<uint32_t>(--self->references);
}

void* __fastcall FakeSelf(Fake* self, void*)
{
    return self;
}

void __fastcall FakeCheckOn(Fake*, void*)
{
    ++g_checks[1];
}

void __fastcall FakeCheckOff(Fake*, void*)
{
    ++g_checks[0];
}

uint8_t __fastcall FakeIsChecked(Fake*, void*)
{
    return g_checked;
}

uint8_t __fastcall FakeWalk(Fake*, void*, uint32_t iid, std::uintptr_t callback, uint32_t value)
{
    g_events.Add(uint8_t{0x20});
    g_events.Add(iid);
    g_events.Add(uint8_t(callback == addr::SC4QueryTool_AttachChildren
                             ? 1
                             : callback == addr::SC4QueryTool_DetachChildren ? 2 : 0));
    g_events.Add(value);
    return 1;
}

uint8_t __fastcall FakeGetChildAs(Fake* self, void*, uint32_t id, uint32_t iid, void** out)
{
    g_events.Add(uint8_t{0x25});
    g_events.Add(id);
    g_events.Add(iid);
    Fake* child = nullptr;
    if (g_children) {
        if (iid == kValueControl || iid == kTextEdit || iid == kCheckControl)
            child = self;
        else if (iid == kTextControl)
            child = &g_text;
        else if (iid == kNumberControl)
            child = &g_number;
    }
    if (!child)
        return 0;
    ++child->references;
    *out = child;
    return 1;
}

void __fastcall FakeEditText(Fake*, void*, const void* text, uint32_t zero)
{
    g_events.Add(uint8_t{0x30});
    g_events.Add(uint8_t{text == g_expectText});
    g_events.Add(zero);
}

void __fastcall FakeEditNumber76(Fake*, void*, uint32_t number)
{
    g_events.Add(uint8_t{0x4C});
    g_events.Add(number);
}

void __fastcall FakeEditNumber78(Fake*, void*, uint32_t number)
{
    g_events.Add(uint8_t{0x4E});
    g_events.Add(number);
}

uint8_t __fastcall FakeGetProperty(Fake*, void*, uint32_t id, void** out)
{
    g_events.Add(uint8_t{0x71});
    g_events.Add(id);
    for (const Property& p : g_properties) {
        if (p.object && p.id == id) {
            Virtual<uint32_t>(p.object, 0x4);
            *out = p.object;
            return 1;
        }
    }
    return 0;
}

uint8_t __fastcall FakeSetProperty(Fake*, void*, uint32_t id, void* object)
{
    g_events.Add(uint8_t{0x72});
    g_events.Add(id);
    g_events.Add(uint8_t{object != nullptr});
    if (object)
        Virtual<uint32_t>(object, 0x4);
    Property* place = nullptr;
    for (Property& p : g_properties) {
        if (p.object && p.id == id) {
            Virtual<uint32_t>(p.object, 0x8);
            p.object = nullptr;
        }
        if (!p.object && !place)
            place = &p;
    }
    if (object && place) {
        place->id = id;
        place->object = object;
    }
    return 1;
}

void __fastcall FakeAttach(Fake*, void*, uint32_t value)
{
    g_events.Add(uint8_t{0x74});
    g_events.Add(value);
}

void __fastcall FakeDetach(Fake*, void*, uint32_t value)
{
    g_events.Add(uint8_t{0x75});
    g_events.Add(value);
}

void __fastcall FakeSetCaption(Fake*, void*, const void* text)
{
    g_events.Add(uint8_t{0x04});
    g_events.Add(uint8_t{text == g_expectText});
}

uint32_t __fastcall FakeGetNumber(Fake*, void*)
{
    g_events.Add(uint8_t{0x05});
    return 0x1234;
}

void __fastcall FakeSinkLine(Fake*, void*, const char* text)
{
    g_sinkText = text ? text : "(null)";
}

void* PropertyObject(uint32_t id)
{
    for (const Property& p : g_properties)
        if (p.object && p.id == id)
            return p.object;
    return nullptr;
}

void ClearProperties()
{
    for (Property& p : g_properties) {
        if (p.object)
            Virtual<uint32_t>(p.object, 0x8);
        p.object = nullptr;
    }
}

void SetUpFakes()
{
    void* const addRef = reinterpret_cast<void*>(&FakeAddRef);
    void* const release = reinterpret_cast<void*>(&FakeRelease);
    for (void** table : {g_windowVtable, g_textVtable, g_numberVtable, g_sinkVtable}) {
        table[1] = addRef;
        table[2] = release;
    }
    g_windowVtable[3] = reinterpret_cast<void*>(&FakeSelf);
    g_windowVtable[6] = reinterpret_cast<void*>(&FakeCheckOn);
    g_windowVtable[7] = reinterpret_cast<void*>(&FakeCheckOff);
    g_windowVtable[8] = reinterpret_cast<void*>(&FakeIsChecked);
    g_windowVtable[32] = reinterpret_cast<void*>(&FakeWalk);
    g_windowVtable[37] = reinterpret_cast<void*>(&FakeGetChildAs);
    g_windowVtable[48] = reinterpret_cast<void*>(&FakeEditText);
    g_windowVtable[76] = reinterpret_cast<void*>(&FakeEditNumber76);
    g_windowVtable[78] = reinterpret_cast<void*>(&FakeEditNumber78);
    g_windowVtable[113] = reinterpret_cast<void*>(&FakeGetProperty);
    g_windowVtable[114] = reinterpret_cast<void*>(&FakeSetProperty);
    g_windowVtable[116] = reinterpret_cast<void*>(&FakeAttach);
    g_windowVtable[117] = reinterpret_cast<void*>(&FakeDetach);
    g_textVtable[4] = reinterpret_cast<void*>(&FakeSetCaption);
    g_numberVtable[4] = reinterpret_cast<void*>(&FakeGetNumber);
    g_sinkVtable[14] = reinterpret_cast<void*>(&FakeSinkLine);
    g_window = {g_windowVtable, 0};
    g_object = {g_windowVtable, 0};
    g_other = {g_windowVtable, 0};
    g_text = {g_textVtable, 0};
    g_number = {g_numberVtable, 0};
    g_sink = {g_sinkVtable, 0};
    for (Property& p : g_properties)
        p = {0, nullptr};
    g_children = true;
    g_checked = 0;
    g_checks[0] = g_checks[1] = 0;
}

// The value a filler left as the stand-in window's property: the two dwords
// and the type word of the game's value object (0x009166D4), then let go.
void TakeValue(Fnv& f)
{
    void* const value = PropertyObject(kValueProperty);
    f.Add(uint8_t{value != nullptr});
    if (value) {
        f.Add(At<uint32_t>(value, 0x4));
        f.Add(At<uint32_t>(value, 0x8));
        f.Add(At<uint16_t>(value, 0xE));
    }
    ClearProperties();
}

// --- 1: the pure functions -------------------------------------------------------------------

uint32_t DrivePure()
{
    Fnv f;
    for (int32_t number = -2; number <= 14; ++number)
        f.Add(Cdecl<int32_t>(addr::SC4QueryTool_KindOfNumber, number));
    const uint32_t kinds = f.h;
    for (int32_t kind = 0; kind <= 4; ++kind) {
        uint32_t first = 0x5A5A5A5A, second = 0xA5A5A5A5;
        f.Add(uint8_t{Cdecl<uint8_t>(addr::SC4QueryTool_TypesOfKind, kind, &first, &second) != 0});
        f.Add(first);
        f.Add(second);
    }
    const uint32_t types = f.h;
    constexpr uint32_t kUsed[] = {0, 1, 79, 80, 81, 90, 99, 100, 101, 799, 800, 801, 1000, 0x80000000, 0xFFFFFFFF};
    constexpr uint32_t kCapacity[] = {0, 1, 100, 1000, 0x80000000, 0xFFFFFFFF};
    for (const uint32_t capacity : kCapacity)
        for (const uint32_t used : kUsed)
            f.Add(FloatBits(static_cast<float>(Cdecl<double>(addr::SC4QueryTool_Share, used, capacity))));
    const uint32_t shares = f.h;
    std::string texts;
    for (const float level : {0.0f, 33.3f, 33.4f, 66.6f, 66.7f, 100.0f, std::numeric_limits<float>::quiet_NaN()}) {
        OwnString text;
        Cdecl<uint32_t>(addr::SC4QueryTool_LevelText, text.Pointer(), level);
        const std::string got = text.Text();
        f.Add(static_cast<uint32_t>(got.size()));
        f.Bytes(got.data(), got.size());
        texts += "'" + got + "' ";
    }
    log::Write("autotest: query1: pure %08X (kinds %08X, types %08X, shares %08X; level texts %s)", f.h, kinds, types,
               shares, texts.c_str());
    return f.h;
}

// --- 2: the filters --------------------------------------------------------------------------

// A filter as the game makes it, with one reference.
char* MakeFilter(std::uintptr_t vtable, std::uintptr_t serialVtable)
{
    char* const filter = Cdecl<char*>(addr::SC4_operator_new, uint32_t{0xC});
    if (!filter)
        return nullptr;
    This<void*>(addr::SC4_OccupantFilterBase, filter);
    At<std::uintptr_t>(filter, 0) = vtable;
    At<std::uintptr_t>(filter, 4) = serialVtable;
    Virtual<uint32_t>(filter, 0x4);
    return filter;
}

uint32_t DriveFilters(const Sample& sample)
{
    static const struct {
        const char* name;
        std::uintptr_t vtable, serialVtable;
    } kFilters[] = {
        {"askable", addr::SC4QueryTool_AskableFilterVtable, 0x00A90998},
        {"building or network", addr::SC4QueryTool_BuildingFilterVtable, 0x00A909CC},
        {"automaton", addr::SC4QueryTool_AutomatonFilterVtable, addr::SC4QueryTool_AutomatonFilterVtable2},
        {"group prop", addr::SC4QueryTool_PropFilterVtable, addr::SC4QueryTool_PropFilterVtable2},
        {"unmarked", addr::SC4QueryTool_UnmarkedFilterVtable, 0x00A90B00},
    };
    constexpr uint32_t kTypes[] = {0x278128A0, 0x2890D4DE, 0x6A11BD2B, 0x74758926, 0xA823821E,
                                   0xC772BF98, 0x496E5783, 0xC7CC75F1, 0x497F6D9D, 0};
    Fnv f;
    f.Add(static_cast<uint32_t>(sample.chosen.size()));
    std::string counts;
    for (const auto& one : kFilters) {
        char* const filter = MakeFilter(one.vtable, one.serialVtable);
        if (!filter)
            continue;
        int32_t yes = 0;
        for (void* const occupant : sample.chosen) {
            const uint8_t answer = Virtual<uint8_t>(filter, 0xC, occupant) != 0;
            f.Add(answer);
            yes += answer;
        }
        uint32_t typeBits = 0, bit = 1;
        for (const uint32_t type : kTypes) {
            if (Virtual<uint8_t>(filter, 0x10, type))
                typeBits |= bit;
            bit <<= 1;
        }
        f.Add(typeBits);
        f.Add(Virtual<uint32_t>(filter, 0x8));
        char text[80];
        std::snprintf(text, sizeof text, "%s %d types %03X; ", one.name, yes, typeBits);
        counts += text;
    }
    log::Write("autotest: query1: filters %08X (%d occupants: %s)", f.h, static_cast<int32_t>(sample.chosen.size()),
               counts.c_str());
    return f.h;
}

// --- 4: the measures of a lot ----------------------------------------------------------------

uint32_t DriveLots(const Sample& sample)
{
    static const std::uintptr_t kLevels[] = {addr::SC4QueryTool_LevelFirst, addr::SC4QueryTool_LevelSecond,
                                             addr::SC4QueryTool_LevelThird, addr::SC4QueryTool_LevelFourth,
                                             addr::SC4QueryTool_LevelFifth};
    Fnv f;
    uint32_t capacitySum = 0, populationSum = 0;
    int32_t developers = 0;
    for (const Building& b : sample.buildings) {
        const uint32_t capacity = Cdecl<uint32_t>(addr::SC4QueryTool_LotCapacity, b.occupant);
        const uint32_t population = Cdecl<uint32_t>(addr::SC4QueryTool_LotPopulation, b.occupant);
        f.Add(capacity);
        f.Add(population);
        f.Add(Cdecl<uint32_t>(addr::SC4QueryTool_CellGradeOf, b.occupant));
        f.Add(FloatBits(static_cast<float>(Cdecl<double>(addr::SC4QueryTool_LotAverage, b.occupant))));
        capacitySum += capacity;
        populationSum += population;
        int32_t box[4] = {};
        Virtual<void>(b.lot, 0x38, static_cast<int32_t*>(box));
        char* const developer = Cdecl<char*>(addr::SC4QueryTool_DeveloperOfLot, b.lot);
        f.Add(uint8_t{developer != nullptr});
        if (!developer)
            continue;
        ++developers;
        for (const std::uintptr_t level : kLevels) {
            int32_t out = 0x5A5A5A5A;
            f.Add(Cdecl<uint32_t>(level, developer, box[0], box[1], &out));
            f.Add(out);
        }
    }
    log::Write("autotest: query1: lots %08X (%d buildings, capacity sum %u, population sum %u, %d with a developer)",
               f.h, static_cast<int32_t>(sample.buildings.size()), capacitySum, populationSum, developers);
    return f.h;
}

// --- 8: the fillers ---------------------------------------------------------------------------

uint32_t DriveFillers(const Sample& sample, uint32_t mask)
{
    Fnv f;
    ClearProperties();
    g_children = true;
    int32_t yes = 0;
    f.Add(mask);
    for (const Building& b : sample.buildings) {
        uint32_t bit = 1;
        for (const uint32_t index : kFillerIndexes) {
            const bool asked = (mask & bit) != 0;
            bit <<= 1;
            if (!asked)
                continue;
            const uint8_t answer =
                Cdecl<uint8_t>(addr::SC4QueryTool_FillControl, &g_window, 0x1000 | index, uint32_t{1}, b.occupant) != 0;
            f.Add(answer);
            yes += answer;
            TakeValue(f);
        }
        int32_t box[4] = {};
        Virtual<void>(b.lot, 0x38, static_cast<int32_t*>(box));
        for (const std::uintptr_t share :
             {addr::SC4QueryTool_FillLotShareFirst, addr::SC4QueryTool_FillLotShareSecond}) {
            f.Add(uint8_t{Cdecl<uint8_t>(share, &g_window, uint32_t{0x77}, uint32_t{1}, box[0], box[1]) != 0});
            TakeValue(f);
            f.Add(uint8_t{Cdecl<uint8_t>(share, &g_window, uint32_t{0x77}, uint32_t{0}, box[0], box[1]) != 0});
        }
    }
    f.Add(uint8_t{Cdecl<uint8_t>(addr::SC4QueryTool_FillCityShare, &g_window, uint32_t{0x78}, uint32_t{1}) != 0});
    TakeValue(f);
    f.Add(g_checks[0]);
    f.Add(g_checks[1]);
    // A control ID without the bit, an index past the table, a null entry.
    const void* const about = sample.buildings.empty() ? nullptr : sample.buildings[0].occupant;
    for (const uint32_t id : {0x29u, 0x107Fu, 0x1005u, 0x10FFu})
        f.Add(uint8_t{Cdecl<uint8_t>(addr::SC4QueryTool_FillControl, &g_window, id, uint32_t{1}, about) != 0});
    log::Write("autotest: query1: fillers %08X (%d buildings, mask %03X, %d fillers said yes; the check box cleared "
               "%d, set %d)",
               f.h, static_cast<int32_t>(sample.buildings.size()), mask, yes, g_checks[0], g_checks[1]);
    return f.h;
}

// --- 16: the window helpers --------------------------------------------------------------------

uint32_t DriveHelpers()
{
    Fnv f;
    g_events = Fnv();
    ClearProperties();
    OwnString text("Query");
    g_expectText = text.Pointer();
    for (const bool children : {true, false}) {
        g_children = children;
        for (Fake* const window : {&g_window, static_cast<Fake*>(nullptr)}) {
            f.Add(uint8_t{Cdecl<uint8_t>(addr::SC4QueryTool_SetChildEdit, window, uint32_t{0x11}, text.Pointer()) !=
                          0});
            f.Add(uint8_t{Cdecl<uint8_t>(addr::SC4QueryTool_SetChildText, window, uint32_t{0x12}, text.Pointer()) !=
                          0});
            uint32_t number = 0x5A5A5A5A;
            f.Add(uint8_t{Cdecl<uint8_t>(addr::SC4QueryTool_GetChildNumber, window, uint32_t{0x13}, &number) != 0});
            f.Add(number);
            f.Add(uint8_t{Cdecl<uint8_t>(addr::SC4QueryTool_SetChildValue, window, uint32_t{0x14}, 1.5f) != 0});
        }
    }
    g_children = true;
    // The value made above is found by a second call and set again.
    f.Add(uint8_t{Cdecl<uint8_t>(addr::SC4QueryTool_SetChildValue, &g_window, uint32_t{0x14}, 0.25f) != 0});
    TakeValue(f);
    const uint32_t controls = f.h;

    // The walks; the second with two of its five properties there.
    f.Add(uint8_t{Cdecl<uint8_t>(addr::SC4QueryTool_AttachChildren, &g_window, uint32_t{0x21}, &g_window,
                                 uint32_t{0x1111}) != 0});
    g_object.references = 1;
    g_other.references = 1;
    FakeSetProperty(&g_window, nullptr, 0x8ADC29D4, &g_object);
    FakeSetProperty(&g_window, nullptr, 0x0AB3F6DB, &g_other);
    f.Add(uint8_t{Cdecl<uint8_t>(addr::SC4QueryTool_DetachChildren, &g_window, uint32_t{0x22}, &g_window,
                                 uint32_t{0x2222}) != 0});
    f.Add(g_object.references);
    f.Add(g_other.references);
    f.Add(uint8_t{PropertyObject(0x8ADC29D4) != nullptr});
    ClearProperties();
    f.Add(g_events.h);
    f.Add(g_window.references);
    f.Add(g_text.references);
    f.Add(g_number.references);
    const uint32_t walks = f.h;

    // The log line.
    using LogFn = void(__cdecl*)(const char*, ...);
    const auto logLine = reinterpret_cast<LogFn>(addr::SC4QueryTool_Log);
    void*& sink = Global<void*>(addr::SC4QueryTool_LogSink);
    void* const sinkBefore = sink;
    g_sinkText = "(none)";
    logLine("q1 %d %s %08X", 7, "seven", 0x1234ABCDu);
    f.Bytes(g_sinkText.data(), g_sinkText.size());
    sink = &g_sink;
    logLine("q1 %d %s %08X", 7, "seven", 0x1234ABCDu);
    f.Bytes(g_sinkText.data(), g_sinkText.size());
    const std::string shortLine = g_sinkText;
    const std::string wide(0x280, 'w');
    logLine("%s", wide.c_str());
    f.Add(static_cast<uint32_t>(g_sinkText.size()));
    sink = sinkBefore;
    g_expectText = nullptr;
    log::Write("autotest: query1: helpers %08X (controls %08X, walks %08X: object references %d and %d, window %d; "
               "log line '%s', a long one cut to %u; a sink was there before %d)",
               f.h, controls, walks, g_object.references, g_other.references, g_window.references, shortLine.c_str(),
               static_cast<uint32_t>(g_sinkText.size()), sinkBefore != nullptr);
    return f.h;
}

// --- 32: the object ---------------------------------------------------------------------------

// The fields of the first piece: the plain ones, and of the pointers whether
// they are set. +0x30 to +0x3F, the padding and the timer are left out.
void HashQuery(Fnv& f, char* tool)
{
    f.Add(At<uint8_t>(tool, base::kInited));
    f.Add(At<uint32_t>(tool, base::kId));
    f.Add(At<uint32_t>(tool, base::kCursor));
    f.Add(uint8_t{At<std::uintptr_t>(tool, 0) == addr::SC4QueryTool_Vtable});
    f.Add(uint8_t{At<std::uintptr_t>(tool, field::kTarget) == addr::SC4QueryTool_TargetVtable});
    for (const std::size_t offset :
         {base::kView, field::kOccupant, field::kTip, field::kPanel, field::kPanelOther, field::kObject9C})
        f.Add(uint8_t{At<void*>(tool, offset) != nullptr});
    f.Add(At<uint32_t>(tool, field::kField40));
    f.Add(At<uint8_t>(tool, field::kFlag78));
    f.Bytes(tool + field::kPlace, 0xC);
    f.Add(At<uint8_t>(tool, field::kFlag88));
    f.Add(At<uint8_t>(tool, field::kPressed));
    f.Add(At<uint32_t>(tool, field::kKind));
    f.Add(At<uint32_t>(tool, field::kSecond));
    f.Add(At<uint8_t>(tool, field::kFlag94));
    f.Add(At<uint32_t>(tool, field::kRouteMode));
}

// A trip record of the test's own: 0x40 bytes, two routes (byte vectors at
// +0 and +0xC), the flag byte at +0x1E, its cell at +0x20, +0x22.
struct Trip {
    uint8_t bytes[0x40];
};

uint32_t DriveObject(char* view, const Sample& sample)
{
    Fnv f;
    char* tools[2] = {};
    for (uint32_t kind = 0; kind < 2; ++kind) {
        char* const memory = Cdecl<char*>(addr::SC4_operator_new, uint32_t{field::kSize});
        if (!memory)
            return f.h;
        std::memset(memory, 0x5A, field::kSize);
        char* const tool = This<char*>(addr::SC4ViewInputControls_F4C4590, memory, kind, uint32_t{0x32});
        f.Add(uint8_t{tool == memory});
        HashQuery(f, memory);
        bool untouched = true;
        for (std::size_t offset = field::kBox; offset < field::kField40; ++offset)
            untouched = untouched && At<uint8_t>(memory, offset) == 0x5A;
        f.Add(uint8_t{untouched});
        Virtual<uint32_t>(memory, 0x4);
        tools[kind] = memory;
    }
    const uint32_t made = f.h;
    char* const tool = tools[0];

    // Names.
    int32_t named = 0;
    std::string firstName;
    const std::size_t names = sample.chosen.size() < 40 ? sample.chosen.size() : 40;
    for (std::size_t i = 0; i < names; ++i) {
        OwnString text;
        At<void*>(tool, field::kOccupant) = sample.chosen[i];
        const uint8_t answer = This<uint8_t>(addr::SC4QueryTool_OccupantName, tool, text.Pointer(), uint32_t{0}) != 0;
        At<void*>(tool, field::kOccupant) = nullptr;
        const std::string got = text.Text();
        f.Add(answer);
        f.Add(static_cast<uint32_t>(got.size()));
        f.Bytes(got.data(), got.size());
        named += answer;
        if (answer && firstName.empty())
            firstName = got;
    }
    const uint32_t namesHash = f.h;

    // Routes: (10,10) kind 1, (11,10) kind 6, (20,20) kind 2; the way back
    // (30,30) kind 3; the record's own cell (50,50).
    uint8_t there[] = {1, 10, 10, 6, 11, 10, 2, 20, 20};
    uint8_t back[] = {3, 30, 30};
    Trip trip = {};
    At<uint8_t*>(trip.bytes, 0x0) = there;
    At<uint8_t*>(trip.bytes, 0x4) = there + sizeof there;
    At<uint8_t*>(trip.bytes, 0x8) = there + sizeof there;
    At<uint8_t*>(trip.bytes, 0xC) = back;
    At<uint8_t*>(trip.bytes, 0x10) = back + sizeof back;
    At<uint8_t*>(trip.bytes, 0x14) = back + sizeof back;
    At<int16_t>(trip.bytes, 0x20) = 50;
    At<int16_t>(trip.bytes, 0x22) = 50;
    static const int32_t kBoxes[][4] = {{11, 10, 11, 10}, {20, 20, 20, 20}, {30, 30, 30, 30},
                                        {50, 50, 50, 50}, {0, 0, 9, 9},     {10, 10, 20, 20}};
    uint32_t kindsSeen = 0;
    for (const uint8_t flag : {uint8_t{0}, uint8_t{1}}) {
        trip.bytes[0x1E] = flag;
        for (const uint32_t wayBack : {0u, 1u}) {
            const uint32_t kinds = This<uint32_t>(addr::SC4QueryTool_RouteKinds, tool, &trip.bytes[0], wayBack);
            f.Add(kinds);
            kindsSeen |= kinds;
            for (const uint32_t mode : {1u, 2u, 3u}) {
                At<uint32_t>(tool, field::kRouteMode) = mode;
                for (const auto& box : kBoxes)
                    f.Add(uint8_t{This<uint8_t>(addr::SC4QueryTool_RouteTouches, tool, &trip.bytes[0],
                                                static_cast<const int32_t*>(box), wayBack) != 0});
            }
        }
    }
    At<uint32_t>(tool, field::kRouteMode) = 3;
    const uint32_t routes = f.h;

    // Without panels nothing happens; the last references.
    This<void>(addr::SC4QueryTool_ShowOtherPanel, tool, uint32_t{1});
    This<void>(addr::SC4QueryTool_ShowOtherPanel, tool, uint32_t{0});
    HashQuery(f, tool);
    for (char* const one : tools)
        f.Add(Virtual<uint32_t>(one, 0x8));
    const uint8_t keys = This<uint8_t>(addr::SC4QueryTool_DebugKeysDown, tool) != 0; // freed memory is not read

    // As the view makes it.
    std::string viewText;
    for (const uint32_t command : {0x6A935CF3u, 0x6A935CF4u}) {
        Virtual<uint8_t>(view, 0x68, uint32_t{0});
        Virtual<uint8_t>(view, 0x1C, command, static_cast<void*>(nullptr), uint32_t{0});
        char* const current = Virtual<char*>(view, 0x60);
        f.Add(uint8_t{current != nullptr});
        if (!current) {
            viewText += "no tool; ";
            continue;
        }
        Virtual<uint32_t>(current, 0x4);
        const bool ours = At<std::uintptr_t>(current, 0) == addr::SC4QueryTool_Vtable;
        f.Add(uint8_t{ours});
        const uint32_t id = Virtual<uint32_t>(current, 0x14);
        f.Add(id);
        uint8_t up = 0xFF;
        if (ours) {
            HashQuery(f, current);
            up = Virtual<uint8_t>(current, 0x48, kOff, kOff, uint32_t{0}) != 0;
            f.Add(up);
            HashQuery(f, current);
        }
        Virtual<uint8_t>(view, 0x68, uint32_t{0});
        const bool away = Virtual<void*>(view, 0x60) == nullptr;
        f.Add(uint8_t{away});
        if (ours)
            HashQuery(f, current);
        const uint32_t last = Virtual<uint32_t>(current, 0x8);
        f.Add(last);
        char text[120];
        std::snprintf(text, sizeof text,
                      "%08X: ID %08X, of the class %d, release answered %u, away %d, last release %u; ", command, id,
                      ours, up, away, last);
        viewText += text;
    }
    log::Write("autotest: query1: object %08X (made %08X; names %08X: %d of %d named, the first '%s'; routes %08X, "
               "kinds seen %X; by the view: %s)",
               f.h, made, namesHash, named, static_cast<int32_t>(names), firstName.c_str(), routes, kindsSeen,
               viewText.c_str());
    log::Write("autotest: query1: varies: the debug keys are down %u", keys);
    return f.h;
}

// --- 64: the picks -----------------------------------------------------------------------------

uint32_t DrivePicks()
{
    Fnv f;
    char* const filter = MakeFilter(addr::SC4QueryTool_AskableFilterVtable, 0x00A90998);
    if (!filter)
        return f.h;
    int32_t found = 0, automata = 0, props = 0, points = 0;
    for (int32_t z = 60; z <= 700; z += 40) {
        for (int32_t x = 40; x <= 980; x += 60) {
            ++points;
            void* occupant = reinterpret_cast<void*>(std::uintptr_t{0x5A5A5A5A});
            const uint8_t picked = Cdecl<uint8_t>(addr::SC4QueryTool_PickOccupant, x, z, &occupant, filter) != 0;
            f.Add(picked);
            f.Add(uint8_t{occupant != nullptr});
            if (picked && occupant) {
                f.Add(Virtual<uint32_t>(occupant, 0x1C));
                Virtual<uint32_t>(occupant, 0x8);
                ++found;
            }
            void* automaton = nullptr;
            if (Cdecl<uint8_t>(addr::SC4QueryTool_PickAutomaton, x, z, &automaton) && automaton) {
                Virtual<uint32_t>(automaton, 0x8);
                ++automata;
            }
            void* prop = nullptr;
            const uint8_t pickedProp = Cdecl<uint8_t>(addr::SC4QueryTool_PickGroupProp, x, z, &prop) != 0;
            f.Add(pickedProp);
            if (pickedProp && prop) {
                Virtual<uint32_t>(prop, 0x8);
                ++props;
            }
        }
    }
    void* nothing = reinterpret_cast<void*>(std::uintptr_t{0x5A5A5A5A});
    f.Add(uint8_t{Cdecl<uint8_t>(addr::SC4QueryTool_PickOccupant, kOff, kOff, &nothing, filter) != 0});
    f.Add(uint8_t{nothing != nullptr});
    f.Add(Virtual<uint32_t>(filter, 0x8));
    log::Write("autotest: query1: picks %08X (%d screen points: %d occupants, %d props of the two groups)", f.h, points,
               found, props);
    log::Write("autotest: query1: varies: %d automata picked", automata);
    return f.h;
}

// --- 128: the small classes ----------------------------------------------------------------------

uint32_t DriveClasses()
{
    Fnv f;
    // The panel target: on the stack without delete, on game memory with.
    std::uintptr_t target[3] = {addr::SC4QueryPanelTarget_Vtable, 7, 0x5A5A5A5A};
    void* const answered = Virtual<void*>(target, 0x14, uint32_t{0});
    f.Add(uint8_t{answered == target});
    f.Add(uint8_t{target[0] == addr::SC4QueryPanelTarget_Vtable});
    f.Add(static_cast<uint32_t>(target[1]));
    f.Add(static_cast<uint32_t>(target[2]));
    if (auto* const made = Cdecl<std::uintptr_t*>(addr::SC4_operator_new, uint32_t{0xC})) {
        made[0] = addr::SC4QueryPanelTarget_Vtable;
        made[1] = 0;
        made[2] = 0;
        f.Add(uint8_t{Virtual<void*>(made, 0x14, uint32_t{1}) == made});
    }
    const uint32_t panel = f.h;

    // The window runner.
    g_window.references = 0;
    g_object.references = 0;
    char* const memory = Cdecl<char*>(addr::SC4_operator_new, uint32_t{runner::kSize});
    if (!memory)
        return f.h;
    std::memset(memory, 0x5A, runner::kSize);
    char* const made = This<char*>(addr::SC4QueryWindowRunner_Construct, memory, &g_window);
    f.Add(uint8_t{made == memory});
    f.Add(uint8_t{At<std::uintptr_t>(memory, 0) == addr::SC4QueryWindowRunner_Vtable});
    f.Add(uint8_t{At<std::uintptr_t>(memory, runner::kProcedure) == addr::SC4QueryWindowRunner_ProcedureVtable});
    f.Add(uint8_t{At<std::uintptr_t>(memory, runner::kTarget) == addr::SC4QueryWindowRunner_TargetVtable});
    f.Add(At<uint32_t>(memory, runner::kCount));
    f.Add(uint8_t{At<void*>(memory, runner::kWindow) == &g_window});
    f.Add(uint8_t{At<void*>(memory, runner::kObject) != nullptr});
    f.Add(g_window.references);
    const int32_t windowReferences = g_window.references;
    std::string answers;
    for (const uint32_t iid : {0x22E85D8Eu, 0x452294AAu, 0x00000001u, 0x12345678u}) {
        void* out = reinterpret_cast<void*>(std::uintptr_t{0x5A5A5A5A});
        const uint8_t answer = Virtual<uint8_t>(memory, 0x0, iid, &out) != 0;
        const int32_t offset = answer && out ? static_cast<int32_t>(static_cast<char*>(out) - memory) : -1;
        f.Add(answer);
        f.Add(offset);
        f.Add(uint8_t{out != nullptr});
        f.Add(At<uint32_t>(memory, runner::kCount));
        char text[48];
        std::snprintf(text, sizeof text, "%08X %u +%d; ", iid, answer, offset);
        answers += text;
    }
    // The two parts' own slot 0 (the thunks).
    for (const std::size_t part : {runner::kProcedure, runner::kTarget}) {
        void* out = nullptr;
        f.Add(uint8_t{Virtual<uint8_t>(memory + part, 0x0, uint32_t{0x452294AA}, &out) != 0});
        f.Add(static_cast<int32_t>(out ? static_cast<char*>(out) - memory : -1));
    }
    f.Add(uint8_t{Virtual<uint8_t>(memory, 0x18, &g_object) != 0});
    f.Add(g_object.references);
    f.Add(uint8_t{At<void*>(memory, runner::kObject) == &g_object});
    f.Add(uint8_t{Virtual<uint8_t>(memory, 0x18, &g_other) != 0});
    f.Add(g_object.references);
    f.Add(uint8_t{Virtual<uint8_t>(memory, 0x18, static_cast<void*>(nullptr)) != 0});
    f.Add(g_other.references);
    f.Add(uint8_t{At<void*>(memory, runner::kObject) != nullptr});
    const uint32_t count = At<uint32_t>(memory, runner::kCount);
    // The runner is left as it is: its last release would run a destructor of
    // a later piece on stand-ins.
    log::Write("autotest: query1: classes %08X (panel target %08X; runner: the window referenced %d times, "
               "QueryInterface %s count %u)",
               f.h, panel, windowReferences, answers.c_str(), count);
    return f.h;
}

// --- The commit ----------------------------------------------------------------------------------

uint32_t Commit(const Sample& sample)
{
    Fnv f;
    if (sample.buildings.empty()) {
        log::Write("autotest: query1: commit: no building");
        return f.h;
    }
    const Building& b = sample.buildings[0];
    ClearProperties();
    g_children = true;
    g_checks[0] = g_checks[1] = 0;
    const uint8_t was = Virtual<uint8_t>(b.lot, 0xAC) != 0;
    g_checked = !was;
    const uint8_t stored =
        Cdecl<uint8_t>(addr::SC4QueryTool_FillControl, &g_window, kHistorical, uint32_t{0}, b.occupant) != 0;
    const uint8_t now = Virtual<uint8_t>(b.lot, 0xAC) != 0;
    const uint8_t shown =
        Cdecl<uint8_t>(addr::SC4QueryTool_FillControl, &g_window, kHistorical, uint32_t{1}, b.occupant) != 0;
    const int32_t cleared = g_checks[0], set = g_checks[1];
    g_checked = was;
    const uint8_t restored =
        Cdecl<uint8_t>(addr::SC4QueryTool_FillControl, &g_window, kHistorical, uint32_t{0}, b.occupant) != 0;
    const uint8_t after = Virtual<uint8_t>(b.lot, 0xAC) != 0;
    g_checked = 0;
    for (const uint8_t one : {was, stored, now, shown, restored, after})
        f.Add(one);
    f.Add(cleared);
    f.Add(set);
    int32_t box[4] = {};
    Virtual<void>(b.lot, 0x38, static_cast<int32_t*>(box));
    f.Bytes(box, sizeof box);
    log::Write("autotest: query1: commit %08X (the lot at %d,%d: historical %u; stored %u, then %u; shown %u: the box "
               "cleared %d, set %d; restored %u, then %u)",
               f.h, box[0], box[1], was, stored, now, shown, cleared, set, restored, after);
    return f.h;
}

void Drive(const Day& day)
{
    if (!OnRunDay(day, "OPENSC4_TEST_Q1") || !game::IsBuild641())
        return;
    char* const view = CityView();
    if (!view || !day.city || !Global<void*>(addr::SC4_City) || !Global<void*>(addr::SC4_LotManager)) {
        log::Write("autotest: query1: no city view");
        return;
    }
    const int parts = EnvInt("OPENSC4_TEST_Q1_PARTS", 255);
    const bool commit = EnvInt("OPENSC4_TEST_Q1_COMMIT", 0) != 0;
    SetUpFakes();
    Sample sample;
    Collect(sample, static_cast<int32_t>(day.city->CellCountX()), static_cast<int32_t>(day.city->CellCountZ()));
    const bool hadTool = Virtual<void*>(view, 0x60) != nullptr;
    log::Write("autotest: query1: %d occupants in the sample, %d buildings; the view had a tool %d",
               static_cast<int32_t>(sample.chosen.size()), static_cast<int32_t>(sample.buildings.size()), hadTool);
    Fnv total;
    total.Add(static_cast<uint32_t>(sample.chosen.size()));
    total.Add(static_cast<uint32_t>(sample.buildings.size()));
    if (parts & 1)
        total.Add(DrivePure());
    if (parts & 2)
        total.Add(DriveFilters(sample));
    if (parts & 4)
        total.Add(DriveLots(sample));
    if (parts & 8) {
        // Filler 1 (bit 0) is the commit's: see the file's head.
        const int mask = EnvInt("OPENSC4_TEST_Q1_FILLERS", commit ? 0xFFF : 0xFFE);
        total.Add(DriveFillers(sample, static_cast<uint32_t>(mask)));
    }
    if (parts & 16)
        total.Add(DriveHelpers());
    if (parts & 32) {
        total.Add(DriveObject(view, sample));
        // The sound of asking: heard only.
        for (std::size_t i = 0; i < sample.buildings.size() && i < 3; ++i)
            Cdecl<void>(addr::SC4QueryTool_QuerySound, sample.buildings[i].occupant);
    }
    if (parts & 64)
        total.Add(DrivePicks());
    if (parts & 128)
        total.Add(DriveClasses());
    if (commit)
        total.Add(Commit(sample));
    ClearProperties();
    std::snprintf(g_token, sizeof g_token, " query1 %08X", total.h);
    log::Write("autotest: query1: total %08X", total.h);
}

const DayHook kQuery1{"query1", 60, &Drive};

void Fingerprint(cISC4City*, std::string& line)
{
    line += g_token;
}

const FingerprintPart kFingerprint{"query1", 21, &Fingerprint};

} // namespace
} // namespace opensc4::autotest
