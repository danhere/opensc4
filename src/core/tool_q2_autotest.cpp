// The query tool's part of the autotest, second piece (src/core/
// autotest_hooks.h; tool_query_2.cpp): calls the functions of 0x004C56F0 to
// 0x004C7A70 and hashes what comes out, so that a run with ui.tools.query2
// on and one with it off can be compared. Only the game's addresses and
// vtable slots are called, never our own functions.
//
//   OPENSC4_TEST_Q2=<day>    token "query2", sub-hashes on the log lines
//     "autotest: query2: ...". Everything here leaves the city as it was.
//     OPENSC4_TEST_Q2_PARTS picks the parts (default all, 31):
//
//   1   helpers on stand-ins: 0x004C6BF0, 0x004C6CA0, 0x004C6D50 and
//       0x004C6E00 over fixed numbers (the caption's text), with and without
//       a window or a child; 0x004C6E30 with a range, without one, with and
//       without the table of IDs (the number control's calls, the three window
//       properties' values, the money text); 0x004C6B10 (the edit's text).
//   2   fillers through the table's reader 0x004D3A90 with fill 1: indexes
//       0, 7, 8, 13, 14, 15, 17, 22, 23 and the port fillers 19, 120, 123
//       to 126 (they call 0x004C7690 to 0x004C78B0) for up to 24 buildings;
//       2, 3, 4 for up to three fire stations and 7 to 11 and 13 to 15 for
//       up to three police stations (a station is a building the
//       simulator's funding getter knows: elsewhere the game reads unset
//       stack in 2, 3, 9 and 10). A save without stations skips them (log).
//   4   levels: 0x004C5C90 and 0x004C5D70 at the first cell of each
//       building's lot with the object 0x004D32D0 gives for the lot.
//   8   object: the tool as the view makes it for 0x6A935CF3 and
//       0x6A935CF4 (Init): its fields, whether the tip and the two windows
//       are there and shown, the windows' places; the tip at the pointer
//       (0x004C5BA0) hidden, shown and hidden again (whether it shows, never
//       where: that is the mouse); the view puts the tool away (the
//       destructor).
//   16  small classes: the window runner made on a counting stand-in and
//       deleted through its slot 3 (0x004C68E0); a text entry destroyed
//       (0x004C6AF0).
//
//   OPENSC4_TEST_Q2_COMMIT=1 adds what changes the city and sets it back:
//   filler 0 with fill 0 renames the first named building and then gives
//   it its name again; fillers 2 and 9 with fill 0 set the first fire and
//   police station's funding to 50 and the test sets the share it had back
//   through the simulator's own setter.
//
// Kept out of the hashes: every pointer (only whether it is set), the
// mouse position and the money texts' locale are not touched (the texts are
// the game's own in both runs).
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
    void Text(const std::string& text)
    {
        Add(static_cast<uint32_t>(text.size()));
        Bytes(text.data(), text.size());
    }
};

char g_token[48];

namespace base = tool_query::base;
namespace field = tool_query::field;
namespace runner = tool_query::window_runner;

constexpr uint32_t kTextEdit = 0x231A1C57, kTextControl = 0x212CDC1F, kNumberControl = 0x21325207;
constexpr uint32_t kBuildingPart = 0x87DDA3A9;
constexpr uint32_t kSliderProperties[] = {0x6A8CC9D6, 0x2AA6E8AD, 0x0AB3F6DB};
constexpr uint32_t kSampleFillers[] = {0, 7, 8, 13, 14, 15, 17, 22, 23, 19, 120, 123, 124, 125, 126};
constexpr uint32_t kFireFillers[] = {2, 3, 4};
constexpr uint32_t kPoliceFillers[] = {7, 8, 9, 10, 11, 13, 14, 15};

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

// The text of a game string (begin and end at +4 and +8).
std::string TextOf(const void* text)
{
    if (!text)
        return "(null)";
    const char* const begin = At<const char*>(const_cast<void*>(text), 0x4);
    const char* const end = At<const char*>(const_cast<void*>(text), 0x8);
    return begin && end >= begin ? std::string(begin, end) : std::string("?");
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
    std::string Text() { return TextOf(&bytes[0]); }
};

// --- The city's buildings and stations -----------------------------------------------------

struct Building {
    void* lot;
    void* occupant;
};

struct Sample {
    std::vector<Building> buildings; // up to 24, every third cell
    std::vector<void*> fire;         // up to three fire stations
    std::vector<void*> police;       // up to three police stations
};

bool IsStation(std::uintptr_t service, std::size_t getter, void* occupant)
{
    void* const simulator = Global<void*>(service);
    if (!simulator)
        return false;
    int32_t cost = 0;
    float share = 0.0f;
    return Virtual<uint8_t>(simulator, getter, occupant, &cost, &share) != 0;
}

void Collect(Sample& out, int32_t sizeX, int32_t sizeZ)
{
    void* const lotManager = Global<void*>(addr::SC4_LotManager);
    if (!lotManager)
        return;
    std::vector<void*> lots;
    for (int32_t z = 0; z < sizeZ; ++z) {
        for (int32_t x = 0; x < sizeX; ++x) {
            void* const lot = Virtual<void*>(lotManager, 0x18, x, z, uint32_t{0});
            void* const building = lot ? Virtual<void*>(lot, 0xC0) : nullptr;
            void* const occupant = building ? Virtual<void*>(building, 0xC) : nullptr;
            if (!occupant || std::find(lots.begin(), lots.end(), lot) != lots.end())
                continue;
            lots.push_back(lot);
            if (out.buildings.size() < 24 && x % 3 == 0 && z % 3 == 0)
                out.buildings.push_back({lot, occupant});
            if (out.fire.size() < 3 && IsStation(addr::SC4_Service43D78, 0x20, occupant))
                out.fire.push_back(occupant);
            if (out.police.size() < 3 && IsStation(addr::SC4_PoliceSimulator, 0x1C, occupant))
                out.police.push_back(occupant);
        }
    }
}

// --- The stand-ins ---------------------------------------------------------------------------

// A counted object with a vtable of ours: a window (that is also its own text
// edit), a text control and a number control.
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
void* g_numberVtable[12];
Fake g_window, g_text, g_number;
Fnv g_events;             // what the stand-ins were asked, in order
Property g_properties[8]; // the stand-in window's properties
bool g_children = true;   // does the stand-in window have children
std::string g_caption;    // the last caption
std::string g_editShown;  // the last text given to the edit
std::string g_editSource; // what the edit hands out
int32_t g_numberValue = 0;

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

void* __fastcall FakeWindowOf(Fake*, void*)
{
    return &g_window;
}

void __fastcall FakeSlot23(Fake*, void*)
{
    g_events.Add(uint8_t{0x23});
}

uint8_t __fastcall FakeGetChildAs(Fake* self, void*, uint32_t id, uint32_t iid, void** out)
{
    g_events.Add(uint8_t{0x25});
    g_events.Add(id);
    g_events.Add(iid);
    Fake* child = nullptr;
    if (g_children) {
        if (iid == kTextEdit)
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

void __fastcall FakeEditGet(Fake*, void*, void* text)
{
    g_events.Add(uint8_t{0x2D});
    Virtual<void>(text, 0x10, g_editSource.c_str());
}

uint32_t __fastcall FakeEditLength(Fake*, void*)
{
    g_events.Add(uint8_t{0x2F});
    return static_cast<uint32_t>(g_editSource.size());
}

void __fastcall FakeEditText(Fake*, void*, const void* text, uint32_t zero)
{
    g_editShown = TextOf(text);
    g_events.Add(uint8_t{0x30});
    g_events.Text(g_editShown);
    g_events.Add(zero);
}

void __fastcall FakeEditNumber(Fake*, void*, uint32_t number)
{
    g_events.Add(uint8_t{0x4C});
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

void __fastcall FakeSetCaption(Fake*, void*, const void* text)
{
    g_caption = TextOf(text);
    g_events.Add(uint8_t{0x04});
    g_events.Text(g_caption);
}

int32_t __fastcall FakeGetNumber(Fake*, void*)
{
    g_events.Add(uint8_t{0x05});
    return g_numberValue;
}

void __fastcall FakeNumberSet(Fake*, void*, int32_t value)
{
    g_events.Add(uint8_t{0x14});
    g_events.Add(value);
}

void __fastcall FakeNumberLow(Fake*, void*, int32_t value)
{
    g_events.Add(uint8_t{0x18});
    g_events.Add(value);
}

void __fastcall FakeNumberHigh(Fake*, void*, int32_t value)
{
    g_events.Add(uint8_t{0x20});
    g_events.Add(value);
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
    for (void** table : {g_windowVtable, g_textVtable, g_numberVtable}) {
        table[1] = addRef;
        table[2] = release;
    }
    g_windowVtable[3] = reinterpret_cast<void*>(&FakeSelf);
    g_windowVtable[23] = reinterpret_cast<void*>(&FakeSlot23);
    g_windowVtable[37] = reinterpret_cast<void*>(&FakeGetChildAs);
    g_windowVtable[45] = reinterpret_cast<void*>(&FakeEditGet);
    g_windowVtable[47] = reinterpret_cast<void*>(&FakeEditLength);
    g_windowVtable[48] = reinterpret_cast<void*>(&FakeEditText);
    g_windowVtable[76] = reinterpret_cast<void*>(&FakeEditNumber);
    g_windowVtable[78] = reinterpret_cast<void*>(&FakeEditNumber);
    g_windowVtable[113] = reinterpret_cast<void*>(&FakeGetProperty);
    g_windowVtable[114] = reinterpret_cast<void*>(&FakeSetProperty);
    g_textVtable[4] = reinterpret_cast<void*>(&FakeSetCaption);
    g_numberVtable[3] = reinterpret_cast<void*>(&FakeWindowOf);
    g_numberVtable[4] = reinterpret_cast<void*>(&FakeGetNumber);
    g_numberVtable[5] = reinterpret_cast<void*>(&FakeNumberSet);
    g_numberVtable[6] = reinterpret_cast<void*>(&FakeNumberLow);
    g_numberVtable[8] = reinterpret_cast<void*>(&FakeNumberHigh);
    g_window = {g_windowVtable, 0};
    g_text = {g_textVtable, 0};
    g_number = {g_numberVtable, 0};
    for (Property& p : g_properties)
        p = {0, nullptr};
    g_children = true;
    g_numberValue = 0;
}

// A game value object (0x009166D4: data or its pointer at +4, the type word
// at +0xE, a count at +0x10): an array (type bit 0x8000) by its dwords, any
// other by the eight bytes at +4. The reference count at +0xC is left out.
void HashValue(Fnv& f, void* value)
{
    f.Add(uint8_t{value != nullptr});
    if (!value)
        return;
    char* const v = static_cast<char*>(value);
    const uint16_t type = At<uint16_t>(v, 0xE);
    const uint32_t count = At<uint32_t>(v, 0x10);
    f.Add(type);
    f.Add(count);
    if (type & 0x8000) {
        const uint32_t* const data = At<const uint32_t*>(v, 0x4);
        f.Add(uint8_t{data != nullptr});
        if (data && count < 0x100)
            f.Bytes(data, count * 4);
    } else {
        f.Bytes(v + 4, 8);
    }
}

// The slider's three properties; then let go.
void TakeSlider(Fnv& f)
{
    for (const uint32_t id : kSliderProperties)
        HashValue(f, PropertyObject(id));
    ClearProperties();
}

// --- 1: the helpers ----------------------------------------------------------------------------

uint32_t DriveHelpers()
{
    Fnv f;
    g_events = Fnv();
    ClearProperties();
    std::string texts;
    constexpr int32_t kNumbers[] = {0, 7, -3, 100, 123456, -2147483647};
    for (const bool children : {true, false}) {
        g_children = children;
        for (Fake* const window : {&g_window, static_cast<Fake*>(nullptr)}) {
            for (const int32_t number : kNumbers) {
                g_caption.clear();
                f.Add(uint8_t{Cdecl<uint8_t>(addr::SC4QueryTool_SetChildInt, window, uint32_t{0x31}, number) != 0});
                f.Text(g_caption);
                g_caption.clear();
                f.Add(uint8_t{Cdecl<uint8_t>(addr::SC4QueryTool_SetChildPercent, window, uint32_t{0x32}, number) != 0});
                f.Text(g_caption);
                g_caption.clear();
                const int64_t amount = static_cast<int64_t>(number) * 1000;
                f.Add(uint8_t{Cdecl<uint8_t>(addr::SC4QueryTool_SetChildMoney, window, uint32_t{0x33}, amount) != 0});
                f.Text(g_caption);
                if (children && window && number == 123456)
                    texts += "'" + g_caption + "' ";
                g_caption.clear();
                f.Add(uint8_t{Cdecl<uint8_t>(addr::SC4QueryTool_SetChildMoneyNext, window, uint32_t{0x34}, amount) !=
                              0});
                f.Text(g_caption);
            }
        }
    }
    g_children = true;
    const uint32_t captions = f.h;

    // Sliders: a range, none, with a table of IDs.
    static const uint32_t kIds[] = {0x101A, 0x101B, 0x101D, 0x101F};
    static const struct {
        int32_t low, high, value;
        int64_t cost;
        const uint32_t* ids;
        uint32_t count;
    } kSliders[] = {{0, 200, 100, 5000, nullptr, 0}, {1, 0, 37, 123456789, nullptr, 0}, {10, 10, 250, -40, kIds, 4}};
    for (const auto& one : kSliders) {
        g_caption.clear();
        f.Add(uint8_t{Cdecl<uint8_t>(addr::SC4QueryTool_SetChildSlider, &g_window, uint32_t{0x41}, one.low, one.high,
                                     one.value, one.cost, static_cast<void*>(nullptr), uint32_t{0x00ABCDEF},
                                     one.ids, one.count) != 0});
        f.Text(g_caption);
        TakeSlider(f);
    }
    f.Add(uint8_t{Cdecl<uint8_t>(addr::SC4QueryTool_SetChildSlider, static_cast<void*>(nullptr), uint32_t{0x41},
                                 int32_t{0}, int32_t{1}, int32_t{1}, int64_t{1}, static_cast<void*>(nullptr),
                                 uint32_t{0}, static_cast<const uint32_t*>(nullptr), uint32_t{0}) != 0});
    const uint32_t sliders = f.h;

    // The edit's text.
    g_editSource = "Edited by q2";
    for (const bool children : {true, false}) {
        g_children = children;
        OwnString text;
        uint32_t length = 0x5A5A5A5A;
        f.Add(uint8_t{Cdecl<uint8_t>(addr::SC4QueryTool_GetChildEdit, &g_window, uint32_t{0x51}, text.Pointer(),
                                     &length) != 0});
        f.Add(length);
        f.Text(text.Text());
    }
    g_children = true;
    f.Add(g_events.h);
    f.Add(g_window.references);
    f.Add(g_text.references);
    f.Add(g_number.references);
    log::Write("autotest: query2: helpers %08X (captions %08X, money of 123456000 %s; sliders %08X; references %d %d %d)",
               f.h, captions, texts.c_str(), sliders, g_window.references, g_text.references, g_number.references);
    return f.h;
}

// --- 2: the fillers ------------------------------------------------------------------------------

uint32_t Fill(Fnv& f, uint32_t index, uint32_t fill, void* occupant)
{
    g_caption.clear();
    g_editShown.clear();
    const uint8_t answer =
        Cdecl<uint8_t>(addr::SC4QueryTool_FillControl, &g_window, 0x1000 | index, fill, occupant) != 0;
    f.Add(index);
    f.Add(answer);
    f.Text(g_caption);
    f.Text(g_editShown);
    HashValue(f, PropertyObject(0xEA563AB0));
    TakeSlider(f);
    return answer;
}

uint32_t DriveFillers(const Sample& sample)
{
    Fnv f;
    ClearProperties();
    g_children = true;
    g_events = Fnv();
    int32_t yes = 0;
    for (const Building& b : sample.buildings)
        for (const uint32_t index : kSampleFillers)
            yes += Fill(f, index, 1, b.occupant);
    for (void* const station : sample.fire)
        for (const uint32_t index : kFireFillers)
            yes += Fill(f, index, 1, station);
    for (void* const station : sample.police)
        for (const uint32_t index : kPoliceFillers)
            yes += Fill(f, index, 1, station);
    f.Add(g_events.h);
    log::Write("autotest: query2: fillers %08X (%d buildings, %d fire and %d police stations; %d fillers said yes)", f.h,
               static_cast<int32_t>(sample.buildings.size()), static_cast<int32_t>(sample.fire.size()),
               static_cast<int32_t>(sample.police.size()), yes);
    if (sample.fire.empty() || sample.police.empty())
        log::Write("autotest: query2: fillers: no fire station %d, no police station %d: their fillers skipped",
                   sample.fire.empty(), sample.police.empty());
    return f.h;
}

// --- 4: the levels -------------------------------------------------------------------------------

uint32_t DriveLevels(const Sample& sample)
{
    Fnv f;
    int32_t developers = 0;
    for (const Building& b : sample.buildings) {
        int32_t box[4] = {};
        Virtual<void>(b.lot, 0x38, static_cast<int32_t*>(box));
        char* const developer = Cdecl<char*>(addr::SC4QueryTool_DeveloperOfLot, b.lot);
        f.Add(uint8_t{developer != nullptr});
        if (!developer)
            continue;
        ++developers;
        for (const std::uintptr_t level : {addr::SC4QueryTool_LevelPollution, addr::SC4QueryTool_LevelGarbage}) {
            int32_t out = 0x5A5A5A5A;
            f.Add(Cdecl<uint32_t>(level, developer, box[0], box[1], &out));
            f.Add(out);
        }
    }
    log::Write("autotest: query2: levels %08X (%d buildings, %d with a developer)", f.h,
               static_cast<int32_t>(sample.buildings.size()), developers);
    return f.h;
}

// --- 8: the object -------------------------------------------------------------------------------

uint8_t Shown(void* window)
{
    return window ? uint8_t{Virtual<uint8_t>(window, 0x11C) != 0} : uint8_t{0xFF};
}

void HashInit(Fnv& f, char* tool)
{
    f.Add(At<uint8_t>(tool, base::kInited));
    f.Add(At<uint32_t>(tool, base::kId));
    f.Add(At<uint32_t>(tool, base::kCursor));
    f.Add(At<uint8_t>(tool, field::kFlag78));
    f.Add(At<uint8_t>(tool, field::kFlag94));
    f.Add(At<uint32_t>(tool, field::kKind));
    void* const tip = At<void*>(tool, field::kTip);
    f.Add(uint8_t{tip != nullptr});
    if (tip)
        f.Add(Shown(Virtual<void*>(tip, 0xC)));
    for (const std::size_t at : {field::kPanel, field::kPanelOther}) {
        void* const window = At<void*>(tool, at);
        f.Add(uint8_t{window != nullptr});
        if (!window)
            continue;
        f.Add(Shown(window));
        f.Add(Virtual<int32_t>(window, 0xA4));
        f.Add(Virtual<int32_t>(window, 0xB0));
        f.Add(Virtual<int32_t>(window, 0xB4));
    }
}

uint32_t DriveObject(char* view)
{
    Fnv f;
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
        uint8_t tips[3] = {0xFF, 0xFF, 0xFF};
        if (ours) {
            HashInit(f, current);
            void* const tip = At<void*>(current, field::kTip);
            OwnString empty;
            OwnString title("Query two");
            const struct {
                void* title;
                void* text;
            } kCalls[] = {{empty.Pointer(), empty.Pointer()}, {title.Pointer(), empty.Pointer()},
                          {nullptr, nullptr}};
            for (int i = 0; i < 3; ++i) {
                This<void>(addr::SC4QueryTool_ShowTip, current, kCalls[i].title, kCalls[i].text);
                tips[i] = tip ? Shown(Virtual<void*>(tip, 0xC)) : uint8_t{0xFE};
                f.Add(tips[i]);
            }
            HashInit(f, current);
        }
        Virtual<uint8_t>(view, 0x68, uint32_t{0});
        const bool away = Virtual<void*>(view, 0x60) == nullptr;
        f.Add(uint8_t{away});
        const uint32_t last = Virtual<uint32_t>(current, 0x8);
        f.Add(last);
        char text[160];
        std::snprintf(text, sizeof text, "%08X: of the class %d, tip shown %u %u %u, away %d, last release %u; ",
                      command, ours, tips[0], tips[1], tips[2], away, last);
        viewText += text;
    }
    log::Write("autotest: query2: object %08X (%s)", f.h, viewText.c_str());
    return f.h;
}

// --- 16: the small classes ------------------------------------------------------------------------

uint32_t DriveClasses()
{
    Fnv f;
    g_window.references = 0;
    char* const memory = Cdecl<char*>(addr::SC4_operator_new, uint32_t{runner::kSize});
    if (memory) {
        This<char*>(addr::SC4QueryWindowRunner_Construct, memory, &g_window);
        const int32_t before = g_window.references;
        char* const answered = Virtual<char*>(memory, 0xC, uint32_t{1});
        f.Add(uint8_t{answered == memory});
        f.Add(before);
        f.Add(g_window.references);
    }
    // A text entry (a key and a string) destroyed.
    char entry[tool_query::text_entry::kSize] = {};
    At<uint32_t>(entry, tool_query::text_entry::kKey) = 7;
    This<void*>(addr::SC4String_FromText, entry + tool_query::text_entry::kText, "entry");
    At<std::uintptr_t>(entry, tool_query::text_entry::kText) = 0x5A5A5A5A;
    This<void>(addr::SC4QueryTool_DestroyTextEntry, entry);
    f.Add(uint8_t{At<std::uintptr_t>(entry, tool_query::text_entry::kText) == addr::SC4String_Vtable});
    f.Add(At<uint32_t>(entry, tool_query::text_entry::kKey));
    log::Write("autotest: query2: classes %08X (the runner's window referenced %d times when it went)", f.h,
               g_window.references);
    return f.h;
}

// --- The commit ----------------------------------------------------------------------------------

uint32_t CommitName(const Sample& sample)
{
    Fnv f;
    void* named = nullptr;
    std::string old;
    for (const Building& b : sample.buildings) {
        void* part = nullptr;
        if (!Virtual<uint8_t>(b.occupant, 0x0, kBuildingPart, &part))
            continue;
        Virtual<uint32_t>(part, 0x8);
        Fill(f, 0, 1, b.occupant);
        if (!g_editShown.empty()) {
            named = b.occupant;
            old = g_editShown;
            break;
        }
    }
    if (!named) {
        log::Write("autotest: query2: commit: no named building");
        return f.h;
    }
    g_editSource = "Q2 renamed";
    const uint32_t renamed = Fill(f, 0, 0, named);
    Fill(f, 0, 1, named);
    const std::string shown = g_editShown;
    g_editSource = old;
    const uint32_t back = Fill(f, 0, 0, named);
    Fill(f, 0, 1, named);
    const bool same = g_editShown == old;
    f.Add(uint8_t{same});
    log::Write("autotest: query2: commit name %08X ('%s' renamed %u to '%s', back %u, the same again %d)", f.h,
               old.c_str(), renamed, shown.c_str(), back, same);
    return f.h;
}

uint32_t CommitFunding(void* station, uint32_t index, std::uintptr_t service, std::size_t getter, std::size_t setter,
                       const char* name)
{
    Fnv f;
    if (!station) {
        log::Write("autotest: query2: commit: no %s station", name);
        return f.h;
    }
    void* const simulator = Global<void*>(service);
    int32_t cost = 0;
    float was = 0.0f, now = 0.0f, after = 0.0f;
    Virtual<uint8_t>(simulator, getter, station, &cost, &was);
    Fill(f, index, 1, station);
    g_numberValue = 50;
    const uint32_t stored = Fill(f, index, 0, station);
    Virtual<uint8_t>(simulator, getter, station, &cost, &now);
    float back = was;
    Virtual<uint8_t>(simulator, setter, station, &back);
    Virtual<uint8_t>(simulator, getter, station, &cost, &after);
    g_numberValue = 0;
    f.Add(FloatBits(was));
    f.Add(FloatBits(now));
    f.Add(FloatBits(after));
    f.Add(cost);
    log::Write("autotest: query2: commit %s %08X (share %08X, set %u to %08X, back %08X)", name, f.h, FloatBits(was),
               stored, FloatBits(now), FloatBits(after));
    return f.h;
}

void Drive(const Day& day)
{
    if (!OnRunDay(day, "OPENSC4_TEST_Q2") || !game::IsBuild641())
        return;
    char* const view = CityView();
    if (!view || !day.city || !Global<void*>(addr::SC4_City) || !Global<void*>(addr::SC4_LotManager)) {
        log::Write("autotest: query2: no city view");
        return;
    }
    const int parts = EnvInt("OPENSC4_TEST_Q2_PARTS", 31);
    const bool commit = EnvInt("OPENSC4_TEST_Q2_COMMIT", 0) != 0;
    SetUpFakes();
    Sample sample;
    Collect(sample, static_cast<int32_t>(day.city->CellCountX()), static_cast<int32_t>(day.city->CellCountZ()));
    log::Write("autotest: query2: %d buildings, %d fire stations, %d police stations",
               static_cast<int32_t>(sample.buildings.size()), static_cast<int32_t>(sample.fire.size()),
               static_cast<int32_t>(sample.police.size()));
    Fnv total;
    total.Add(static_cast<uint32_t>(sample.buildings.size()));
    total.Add(static_cast<uint32_t>(sample.fire.size()));
    total.Add(static_cast<uint32_t>(sample.police.size()));
    if (parts & 1)
        total.Add(DriveHelpers());
    if (parts & 2)
        total.Add(DriveFillers(sample));
    if (parts & 4)
        total.Add(DriveLevels(sample));
    if (parts & 8)
        total.Add(DriveObject(view));
    if (parts & 16)
        total.Add(DriveClasses());
    if (commit) {
        total.Add(CommitName(sample));
        total.Add(CommitFunding(sample.fire.empty() ? nullptr : sample.fire[0], 2, addr::SC4_Service43D78, 0x20, 0x24,
                                "fire"));
        total.Add(CommitFunding(sample.police.empty() ? nullptr : sample.police[0], 9, addr::SC4_PoliceSimulator, 0x1C,
                                0x20, "police"));
    }
    ClearProperties();
    std::snprintf(g_token, sizeof g_token, " query2 %08X", total.h);
    log::Write("autotest: query2: total %08X", total.h);
}

const DayHook kQuery2{"query2", 61, &Drive};

void Fingerprint(cISC4City*, std::string& line)
{
    line += g_token;
}

const FingerprintPart kFingerprint{"query2", 22, &Fingerprint};

} // namespace
} // namespace opensc4::autotest
