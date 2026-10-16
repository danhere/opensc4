// The query tool's part of the autotest, sixth piece (src/core/
// autotest_hooks.h; tool_query_6a.cpp, tool_query_6b.cpp): calls the
// functions of 0x004D07C0 to 0x004D4140 and hashes what comes out, so that a
// run with ui.tools.query6a/6b on and one with them off can be compared.
// Only the game's addresses and vtable slots are called, never our own
// functions.
//
//   OPENSC4_TEST_Q6=<day>    token "query6", sub-hashes on the log lines
//     "autotest: query6: ...". OPENSC4_TEST_Q6_PARTS picks the parts
//     (default 31; 63 with OPENSC4_TEST_Q6_COMMIT=1):
//
//   1   the fillers 50, 57, 63, 83, 84, 85, 87, 89, 90, 116, 117 and 121
//       through 0x004D3A90 on a stand-in window, for the sample's buildings,
//       power plants, water producers and abandoned lots, with fill 1 and
//       0, and without an occupant.
//   2   the helpers called directly: 0x004D0D30 (money with cents) for a
//       few amounts, 0x004D3260 and 0x004D32D0 for the sample's lots,
//       0x004D3470 with each level function and both name tables.
//   4   the tool of kind 1 as the view makes it: 0x004D07C0 for one network
//       piece of each network type the city has, and a building.
//   8   the walks 0x004D3AE0, 0x004D3B40, 0x004D3BD0 on the stand-in (as a
//       text edit, as 0x8810, as neither; IDs of fillers that read nothing
//       back without fill, and one without the bit 0x1000), and the window
//       runner's procedure 0x004D3F40 with a stand-in runner and slider
//       (wrong window, wrong type, unknown message, an end with 2, a slider
//       move with a cost, a function of the test's own and IDs to fill).
//   16  the cell log 0x004D12A0 at a few cells, the current directory moved
//       to a folder of the test's own meanwhile (the game writes Query.txt
//       relative to it: the install folder otherwise); the file's bytes
//       hashed, the file and the folder deleted.
//   32  (only with OPENSC4_TEST_Q6_COMMIT=1) the query dialog 0x004D3C30
//       for up to three buildings with a query layout, with a stand-in for
//       the window manager's DoModalWin (slot 41), answered 1 and then 0.
//       It changes the city: the fill walk reaches every filler of the
//       dialog (filler 1 caches the lot's road access), and the answer 0
//       reads the dialog back (fillers without fill: the name, funding,
//       historical); nothing of it is set back. Compare A and B commit legs
//       with each other only.
//   The tool is put away by the view (slot 0x68: Shutdown) at the end.
//
// Kept out of the hashes: every pointer (only whether it is set).
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

#include <windows.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
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

namespace field = tool_query::field;
namespace window_message = tool_query::window_message;

constexpr uint32_t kTextControl = 0x212CDC1F, kNumberControl = 0x21325207, kPictureControl = 0xC12CEA13;
constexpr uint32_t kTextEdit = 0x231A1C57, kOtherSkipped = 0x8810;
constexpr uint32_t kTypeNetwork = 0xC772BF98;
constexpr uint32_t kNetworkOccupant = 0xA821EF94;
constexpr uint32_t kLayoutProperty = 0x2A499F85;
constexpr uint32_t kLeftProperty = 0x8ADC29D4, kValueProperty = 0xEA563AB0;
constexpr uint32_t kCostProperty = 0x2AA6E8AD, kHandlerProperty = 0x6A8CC9D6, kIdsProperty = 0x0AB3F6DB;
constexpr uint32_t kFillers[] = {50, 57, 63, 83, 84, 85, 87, 89, 90, 116, 117, 121};

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

std::string TextOf(const void* text)
{
    if (!text)
        return "(null)";
    const char* const begin = At<const char*>(const_cast<void*>(text), 0x4);
    const char* const end = At<const char*>(const_cast<void*>(text), 0x8);
    return begin && end >= begin ? std::string(begin, end) : std::string("?");
}

// An SC4String of the game's (0x00578390 makes it empty), its text read and
// its buffer freed after.
struct Text {
    uint32_t words[5] = {};
    Text()
    {
        This<void*>(addr::SC4String_SC4String, words);
    }
    ~Text()
    {
        words[0] = static_cast<uint32_t>(addr::SC4String_Vtable);
        if (words[1])
            Cdecl<void>(addr::SC4_Free, reinterpret_cast<void*>(static_cast<std::uintptr_t>(words[1])));
    }
    Text(const Text&) = delete;
    Text& operator=(const Text&) = delete;
    std::string Read() const
    {
        return TextOf(words);
    }
};

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

// --- The stand-ins ---------------------------------------------------------------------------

struct Fake {
    void** vtable;
    int32_t references;
};

void* g_windowVtable[128];
void* g_textVtable[8];
void* g_pictureVtable[8];
void* g_partVtable[128];
void* g_numberVtable[8];
void* g_sliderPartVtable[128];
void* g_procedureVtable[8];
void* g_valueVtable[112];
Fake g_window, g_text, g_picture, g_part, g_number, g_sliderPart, g_procedure;
Fake g_cost, g_handler, g_ids;
Fnv g_events;
std::string g_caption;

// What the stand-in window answers to QueryInterface (the walks ask).
bool g_isEdit = false;
bool g_isOther = false;

// The picture's window: its rectangle and the properties kept on it.
int32_t g_rect[4] = {10, 4, 110, 16};
struct Property {
    uint32_t id;
    void* value;
};
Property g_kept[8];
int32_t g_keptCount = 0;

// The slider's figures.
int32_t g_sliderValue = 40;
int64_t g_sliderCost = 123457;
uint32_t g_sliderIds[3] = {};
uint32_t g_sliderIdCount = 0;
int32_t g_handled = 0;

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

uint8_t __fastcall FakeQueryInterface(Fake* self, void*, uint32_t iid, void** out)
{
    g_events.Add(uint8_t{0x00});
    g_events.Add(iid);
    if ((iid == kTextEdit && g_isEdit) || (iid == kOtherSkipped && g_isOther)) {
        ++self->references;
        *out = self;
        return 1;
    }
    return 0;
}

void* __fastcall FakeParent(Fake*, void*)
{
    g_events.Add(uint8_t{0x0B});
    return &g_window;
}

void* __fastcall FakeNoChild(Fake*, void*, uint32_t id)
{
    g_events.Add(uint8_t{0x23});
    g_events.Add(id);
    return nullptr;
}

uint8_t __fastcall FakeGetChildAs(Fake*, void*, uint32_t id, uint32_t iid, void** out)
{
    g_events.Add(uint8_t{0x25});
    g_events.Add(id);
    g_events.Add(iid);
    Fake* child = nullptr;
    if (iid == kTextControl)
        child = &g_text;
    else if (iid == kNumberControl)
        child = &g_number;
    else if (iid == kPictureControl)
        child = &g_picture;
    if (!child)
        return 0;
    ++child->references;
    *out = child;
    return 1;
}

// Window slot 32: what the walks give for the children; recorded, not run.
uint8_t __fastcall FakeForEachChild(Fake*, void*, uint32_t iid, std::uintptr_t walk, void* value)
{
    g_events.Add(uint8_t{0x20});
    g_events.Add(iid);
    g_events.Add(static_cast<uint32_t>(walk));
    g_events.Add(uint8_t{value != nullptr});
    return 1;
}

void __fastcall FakeSetCaption(Fake*, void*, const void* text)
{
    g_caption = TextOf(text);
    g_events.Add(uint8_t{0x04});
    g_events.Text(g_caption);
}

void* __fastcall FakePart(Fake*, void*)
{
    g_events.Add(uint8_t{0x03});
    return &g_part;
}

const int32_t* __fastcall FakeRect(Fake*, void*)
{
    g_events.Add(uint8_t{0x30});
    return g_rect;
}

void __fastcall FakeSetRect(Fake*, void*, const int32_t* rect)
{
    g_events.Add(uint8_t{0x36});
    g_events.Bytes(rect, 4 * sizeof(int32_t));
}

// What a property value holds as a dword (its slot 73 gives where).
uint32_t ValueWord(void* value)
{
    return *Virtual<const uint32_t*>(value, 0x124);
}

uint8_t __fastcall FakeGetProperty(Fake*, void*, uint32_t id, void** out)
{
    g_events.Add(uint8_t{0x71});
    g_events.Add(id);
    for (int32_t i = 0; i < g_keptCount; ++i) {
        if (g_kept[i].id == id) {
            Virtual<uint32_t>(g_kept[i].value, 0x4);
            *out = g_kept[i].value;
            return 1;
        }
    }
    return 0;
}

// The values the game makes here (script numbers) are kept with the
// reference they come with, as a window would; never let go (the game's
// windows would, at their end; the same on both sides).
void __fastcall FakeSetProperty(Fake*, void*, uint32_t id, void* value)
{
    g_events.Add(uint8_t{0x72});
    g_events.Add(id);
    g_events.Add(uint8_t{value != nullptr});
    if (value)
        g_events.Add(ValueWord(value));
    for (int32_t i = 0; i < g_keptCount; ++i) {
        if (g_kept[i].id == id) {
            g_kept[i].value = value;
            return;
        }
    }
    if (g_keptCount < 8)
        g_kept[g_keptCount++] = {id, value};
}

int32_t __fastcall FakeSliderValue(Fake*, void*)
{
    g_events.Add(uint8_t{0x10});
    return g_sliderValue;
}

void* __fastcall FakeSliderPart(Fake*, void*)
{
    g_events.Add(uint8_t{0x13});
    return &g_sliderPart;
}

uint32_t __fastcall FakeSliderId(Fake*, void*)
{
    g_events.Add(uint8_t{0x3F});
    return 0x7A11;
}

uint8_t __fastcall FakeSliderProperty(Fake*, void*, uint32_t id, void** out)
{
    g_events.Add(uint8_t{0x73});
    g_events.Add(id);
    Fake* value = nullptr;
    if (id == kCostProperty)
        value = &g_cost;
    else if (id == kHandlerProperty)
        value = &g_handler;
    else if (id == kIdsProperty)
        value = &g_ids;
    if (!value)
        return 0;
    ++value->references;
    *out = value;
    return 1;
}

const int64_t* __fastcall FakeCost(Fake*, void*)
{
    return &g_sliderCost;
}

uint8_t __cdecl Handler(void* window, uint32_t id, uint32_t fill, void* about)
{
    ++g_handled;
    g_events.Add(uint8_t{0x48});
    g_events.Add(uint8_t{window == &g_window});
    g_events.Add(id);
    g_events.Add(fill);
    g_events.Add(uint8_t{about != nullptr});
    return 1;
}

void __fastcall FakeHandler(Fake*, void*, std::uintptr_t* out)
{
    *out = reinterpret_cast<std::uintptr_t>(&Handler);
}

const uint32_t* __fastcall FakeIds(Fake*, void*)
{
    return g_sliderIds;
}

uint32_t __fastcall FakeIdCount(Fake*, void*)
{
    return g_sliderIdCount;
}

void* __fastcall FakeProcedureWindow(Fake*, void*)
{
    return &g_window;
}

void SetUpFakes()
{
    void* const addRef = reinterpret_cast<void*>(&FakeAddRef);
    void* const release = reinterpret_cast<void*>(&FakeRelease);
    for (void** table : {g_windowVtable, g_textVtable, g_pictureVtable, g_partVtable, g_numberVtable,
                         g_sliderPartVtable, g_procedureVtable, g_valueVtable}) {
        table[1] = addRef;
        table[2] = release;
    }
    g_windowVtable[0] = reinterpret_cast<void*>(&FakeQueryInterface);
    g_windowVtable[3] = reinterpret_cast<void*>(&FakeSelf);
    g_windowVtable[11] = reinterpret_cast<void*>(&FakeParent);
    g_windowVtable[32] = reinterpret_cast<void*>(&FakeForEachChild);
    g_windowVtable[35] = reinterpret_cast<void*>(&FakeNoChild);
    g_windowVtable[37] = reinterpret_cast<void*>(&FakeGetChildAs);
    g_textVtable[4] = reinterpret_cast<void*>(&FakeSetCaption);
    g_pictureVtable[3] = reinterpret_cast<void*>(&FakePart);
    g_partVtable[48] = reinterpret_cast<void*>(&FakeRect);
    g_partVtable[54] = reinterpret_cast<void*>(&FakeSetRect);
    g_partVtable[113] = reinterpret_cast<void*>(&FakeGetProperty);
    g_partVtable[114] = reinterpret_cast<void*>(&FakeSetProperty);
    g_numberVtable[3] = reinterpret_cast<void*>(&FakeSliderPart);
    g_numberVtable[4] = reinterpret_cast<void*>(&FakeSliderValue);
    g_sliderPartVtable[63] = reinterpret_cast<void*>(&FakeSliderId);
    g_sliderPartVtable[113] = reinterpret_cast<void*>(&FakeSliderProperty);
    g_procedureVtable[3] = reinterpret_cast<void*>(&FakeProcedureWindow);
    g_valueVtable[4] = reinterpret_cast<void*>(&FakeIdCount);
    g_valueVtable[52] = reinterpret_cast<void*>(&FakeHandler);
    g_valueVtable[79] = reinterpret_cast<void*>(&FakeCost);
    g_valueVtable[103] = reinterpret_cast<void*>(&FakeIds);
    g_window = {g_windowVtable, 0};
    g_text = {g_textVtable, 0};
    g_picture = {g_pictureVtable, 0};
    g_part = {g_partVtable, 0};
    g_number = {g_numberVtable, 0};
    g_sliderPart = {g_sliderPartVtable, 0};
    g_procedure = {g_procedureVtable, 0};
    g_cost = {g_valueVtable, 0};
    g_handler = {g_valueVtable, 0};
    g_ids = {g_valueVtable, 0};
    g_keptCount = 0;
}

// --- The city's occupants -------------------------------------------------------------------

struct Building {
    void* lot;
    void* occupant;
    int32_t x, z;
};

struct Sample {
    std::vector<Building> buildings; // up to 24, every third cell
    std::vector<Building> plants;    // up to 2 power plants (power slot 33 gives a first figure)
    std::vector<Building> pumps;     // up to 2 water producers with a production
    std::vector<Building> abandoned; // up to 3 abandoned lots (state 4) with a building
    std::vector<void*> pieces;       // one network piece per network type 0 to 12 the city has, referenced
    std::vector<uint32_t> pieceTypes;
    std::vector<std::pair<int32_t, int32_t>> pieceCells;
};

bool CellOf(void* occupant, int32_t& x, int32_t& z)
{
    float position[3];
    Virtual<void>(occupant, 0x20, static_cast<float*>(position));
    x = 0;
    z = 0;
    Virtual<uint8_t>(Global<void*>(addr::SC4_City), 0x17C, position[0], position[2], &x, &z);
    return true;
}

bool IsPlant(void* occupant)
{
    uint32_t first = 0, second = 0;
    Virtual<void>(Global<void*>(addr::SC4_Service43DA0), 0x84, occupant, &first, &second);
    return first != 0;
}

bool IsPump(void* occupant)
{
    uint32_t production = 0;
    const uint8_t said = Virtual<uint8_t>(Global<void*>(addr::SC4_Service43D94), 0x60, occupant, &production,
                                          static_cast<uint32_t*>(nullptr), static_cast<uint32_t*>(nullptr),
                                          static_cast<uint8_t*>(nullptr), static_cast<uint32_t*>(nullptr));
    return said && production != 0;
}

void Collect(Sample& out, int32_t sizeX, int32_t sizeZ)
{
    void* const lotManager = Global<void*>(addr::SC4_LotManager);
    std::vector<void*> lots;
    for (int32_t z = 0; z < sizeZ; ++z) {
        for (int32_t x = 0; x < sizeX; ++x) {
            void* const lot = Virtual<void*>(lotManager, 0x18, x, z, uint32_t{0});
            void* const building = lot ? Virtual<void*>(lot, 0xC0) : nullptr;
            void* const occupant = building ? Virtual<void*>(building, 0xC) : nullptr;
            if (!occupant || std::find(lots.begin(), lots.end(), lot) != lots.end())
                continue;
            lots.push_back(lot);
            const Building b{lot, occupant, x, z};
            if (x % 3 == 0 && z % 3 == 0 && out.buildings.size() < 24)
                out.buildings.push_back(b);
            if (out.plants.size() < 2 && IsPlant(occupant))
                out.plants.push_back(b);
            if (out.pumps.size() < 2 && IsPump(occupant))
                out.pumps.push_back(b);
            if (out.abandoned.size() < 3 && Virtual<uint32_t>(lot, 0x84) == 4)
                out.abandoned.push_back(b);
        }
    }
    void** const sentinel = static_cast<void**>(Virtual<void*>(ListNodeAllocator(), 0xC, uint32_t{0xC}));
    sentinel[0] = sentinel;
    sentinel[1] = sentinel;
    void* list = sentinel;
    int32_t first[2] = {0, 0};
    int32_t last[2] = {sizeX - 1, sizeZ - 1};
    void* const occupants = Global<void*>(addr::SC4_NetworkService);
    Virtual<void>(occupants, 0xA4, &list, static_cast<int32_t*>(first), static_cast<int32_t*>(last), uint32_t{0},
                  uint32_t{0xFFFFFFFF});
    void** const end = static_cast<void**>(list);
    bool had[13] = {};
    for (void** node = static_cast<void**>(end[0]); node != end; node = static_cast<void**>(node[0])) {
        void* const occupant = node[2];
        if (Virtual<uint32_t>(occupant, 0x1C) != kTypeNetwork)
            continue;
        void* network = nullptr;
        if (!Virtual<uint8_t>(occupant, 0x0, kNetworkOccupant, &network))
            continue;
        for (uint32_t type = 0; type < 13; ++type) {
            if (had[type] || !Virtual<uint8_t>(network, 0x58, type))
                continue;
            had[type] = true;
            Virtual<uint32_t>(occupant, 0x4);
            out.pieces.push_back(occupant);
            out.pieceTypes.push_back(type);
            int32_t x = 0, z = 0;
            CellOf(occupant, x, z);
            out.pieceCells.push_back({x, z});
            break;
        }
        Virtual<uint32_t>(network, 0x8);
    }
    Virtual<void>(occupants, 0xC0, &list);
    This<void>(addr::SC4List_Destroy2, &list);
}

uint32_t Part(const char* name, Fnv& f, const char* more)
{
    f.Add(g_events.h);
    log::Write("autotest: query6: %s %08X (%s)", name, f.h, more);
    return f.h;
}

// --- 1: the fillers ---------------------------------------------------------------------------

uint32_t DriveFillers(const Sample& s)
{
    Fnv f;
    g_events = Fnv();
    int32_t said = 0;
    const auto fill = [&](uint32_t index, uint32_t value, void* occupant) {
        g_caption.clear();
        f.Add(Cdecl<uint8_t>(addr::SC4QueryTool_FillControl, &g_window, 0x1000 | index, value, occupant));
        f.Text(g_caption);
        said += !g_caption.empty();
    };
    std::vector<Building> all = s.buildings;
    all.insert(all.end(), s.plants.begin(), s.plants.end());
    all.insert(all.end(), s.pumps.begin(), s.pumps.end());
    all.insert(all.end(), s.abandoned.begin(), s.abandoned.end());
    for (const Building& b : all)
        for (const uint32_t index : kFillers)
            for (const uint32_t value : {1u, 0u})
                fill(index, value, b.occupant);
    for (const uint32_t index : kFillers)
        fill(index, 1, nullptr);
    // What the picture's window keeps: the bar's first left edge (filler
    // 83) and the control's value (0x004C3CA0), as their words.
    f.Add(g_keptCount);
    for (const uint32_t id : {kLeftProperty, kValueProperty}) {
        for (int32_t i = 0; i < g_keptCount; ++i) {
            if (g_kept[i].id == id && g_kept[i].value)
                f.Add(ValueWord(g_kept[i].value));
        }
    }
    char more[120];
    std::snprintf(more, sizeof more, "%d occupants (%d plants, %d pumps, %d abandoned), %d captions",
                  static_cast<int32_t>(all.size()), static_cast<int32_t>(s.plants.size()),
                  static_cast<int32_t>(s.pumps.size()), static_cast<int32_t>(s.abandoned.size()), said);
    return Part("fillers", f, more);
}

// --- 2: the helpers ---------------------------------------------------------------------------

uint32_t DriveHelpers(const Sample& s)
{
    Fnv f;
    g_events = Fnv();
    for (const double amount : {0.0, 1.5, -2.25, 1234567.891, 0.005, 1.0e10}) {
        g_caption.clear();
        f.Add(Cdecl<uint8_t>(addr::SC4QueryTool_SetChildMoneyFloat, &g_window, uint32_t{0x1234}, amount));
        f.Text(g_caption);
    }
    f.Add(Cdecl<uint8_t>(addr::SC4QueryTool_SetChildMoneyFloat, static_cast<void*>(nullptr), uint32_t{0x1234}, 3.0));
    f.Add(Global<uint32_t>(addr::SC4QueryTool_MoneySignGuard) & 1u);
    f.Text(TextOf(reinterpret_cast<void*>(addr::SC4QueryTool_MoneySign)));

    const std::uintptr_t levels[] = {addr::SC4QueryTool_LevelFirst,     addr::SC4QueryTool_LevelSecond,
                                     addr::SC4QueryTool_LevelThird,     addr::SC4QueryTool_LevelFourth,
                                     addr::SC4QueryTool_LevelFifth,     addr::SC4QueryTool_LevelPollution,
                                     addr::SC4QueryTool_LevelGarbage};
    std::vector<Building> all = s.buildings;
    all.insert(all.end(), s.abandoned.begin(), s.abandoned.end());
    int32_t developers = 0;
    for (const Building& b : all) {
        f.Add(Cdecl<uint32_t>(addr::SC4QueryTool_LotWealth, b.lot));
        void* const developer = Cdecl<void*>(addr::SC4QueryTool_DeveloperOfLot, b.lot);
        f.Add(uint8_t{developer != nullptr});
        if (developer) {
            ++developers;
            f.Add(Virtual<uint32_t>(developer, 0x18));
        }
        for (const std::uintptr_t level : levels) {
            for (const std::uintptr_t names : {addr::SC4QueryTool_LevelNames, addr::SC4QueryTool_LevelNamesFifth}) {
                Text text;
                Cdecl<void>(addr::SC4QueryTool_OccupantLevelText, b.occupant, level, names,
                            static_cast<void*>(text.words));
                f.Text(text.Read());
            }
        }
    }
    char more[80];
    std::snprintf(more, sizeof more, "%d lots, %d developers", static_cast<int32_t>(all.size()), developers);
    return Part("helpers", f, more);
}

// --- 4: on the tool ---------------------------------------------------------------------------

// The occupant at +0x2C replaced by `occupant` (referenced), its own
// highlight at +0x40, as OnMouseMove leaves it.
void SetOccupant(char* tool, void* occupant)
{
    if (void* const old = At<void*>(tool, field::kOccupant))
        Virtual<uint32_t>(old, 0x8);
    At<void*>(tool, field::kOccupant) = occupant;
    if (occupant) {
        Virtual<uint32_t>(occupant, 0x4);
        At<uint32_t>(tool, field::kField40) = Virtual<uint32_t>(occupant, 0x40);
    } else {
        At<uint32_t>(tool, field::kField40) = 0;
    }
}

uint32_t DriveTraffic(char* tool, const Sample& s)
{
    Fnv f;
    g_events = Fnv();
    std::string types;
    std::size_t longest = 0;
    std::vector<void*> asked = s.pieces;
    if (!s.buildings.empty())
        asked.push_back(s.buildings[0].occupant);
    for (std::size_t i = 0; i < asked.size(); ++i) {
        SetOccupant(tool, asked[i]);
        Text title, text;
        This<void>(addr::SC4QueryTool_NetworkTrafficInfo, tool, static_cast<void*>(title.words),
                   static_cast<void*>(text.words));
        f.Text(title.Read());
        f.Text(text.Read());
        longest = std::max(longest, text.Read().size());
        if (i < s.pieceTypes.size()) {
            char one[8];
            std::snprintf(one, sizeof one, "%u ", s.pieceTypes[i]);
            types += one;
        }
    }
    char more[120];
    std::snprintf(more, sizeof more, "types %s; longest text %d", types.c_str(), static_cast<int32_t>(longest));
    return Part("traffic", f, more);
}

// --- 8: the walks and the runner's procedure ----------------------------------------------------

uint32_t DriveWalks(const Sample& s)
{
    Fnv f;
    g_events = Fnv();
    // Without a building the fillers are not reached (0x004D3A90 says no
    // without `about`): only the walks' own steps are.
    void* const about = s.buildings.empty() ? nullptr : s.buildings[0].occupant;
    // Fillers that do nothing without fill (57, 63, 84), one that does
    // nothing with it for a stand-in (121 with no lot), and an ID without
    // the bit 0x1000.
    const uint32_t ids[] = {0x1000 | 57, 0x1000 | 63, 0x1000 | 84, 0x1000 | 121, 0x0005, 0x1000 | 0x7F};
    const std::uintptr_t walks[] = {addr::SC4QueryTool_FillChildren, addr::SC4QueryTool_FillChildrenNotEdits};
    for (int kind = 0; kind < 3; ++kind) {
        g_isEdit = kind == 1;
        g_isOther = kind == 2;
        for (const uint32_t id : ids) {
            for (const std::uintptr_t walk : walks) {
                g_caption.clear();
                f.Add(Cdecl<uint8_t>(walk, &g_window, id, &g_window, about));
                f.Text(g_caption);
            }
            if (id == (0x1000 | 121))
                continue;
            f.Add(Cdecl<uint8_t>(addr::SC4QueryTool_ReadChildren, &g_window, id, &g_window, about));
        }
    }
    g_isEdit = false;
    g_isOther = false;

    // The runner: +0x10 the procedure part (its slot 3 the stand-in window),
    // +0x14 the occupant; the procedure is called on its +0x8.
    alignas(4) char runner[tool_query::window_runner::kSize] = {};
    At<void*>(runner, tool_query::window_runner::kWindow) = &g_procedure;
    At<void*>(runner, tool_query::window_runner::kObject) = about;
    g_sliderIds[0] = 0x1000 | 57;
    g_sliderIds[1] = 0x1000 | 84;
    g_sliderIds[2] = 0x1000 | 121;
    g_sliderIdCount = 3;
    g_handled = 0;
    Fake other = {g_windowVtable, 0};
    const auto send = [&](void* window, uint32_t type, uint32_t what, uint32_t data) {
        uint32_t message[3] = {type, what, data};
        g_caption.clear();
        f.Add(This<uint8_t>(addr::SC4QueryWindowRunner_OnMessage, runner + tool_query::window_runner::kProcedure,
                            window, static_cast<uint32_t*>(message)));
        f.Text(g_caption);
    };
    send(&other, window_message::kCommand, window_message::kSliderMoved, 0x5555);
    send(&g_window, 2, window_message::kSliderMoved, 0x5555);
    send(&g_window, window_message::kCommand, 0x12345678, 0x5555);
    send(&g_window, window_message::kCommand, window_message::kEnd, 2);
    for (const int64_t cost : {int64_t{123457}, int64_t{0}, int64_t{-99}, int64_t{987654321987}}) {
        g_sliderCost = cost;
        for (const int32_t value : {40, 0, 100, 7}) {
            g_sliderValue = value;
            send(&g_window, window_message::kCommand, window_message::kSliderMoved, 0x5555);
        }
    }
    At<void*>(runner, tool_query::window_runner::kObject) = nullptr;
    send(&g_window, window_message::kCommand, window_message::kSliderMoved, 0x5555);
    f.Add(g_handled);
    f.Add(g_cost.references);
    f.Add(g_handler.references);
    f.Add(g_ids.references);
    f.Add(g_number.references);
    char more[80];
    std::snprintf(more, sizeof more, "handler called %d times", g_handled);
    return Part("walks", f, more);
}

// --- 16: the cell log -------------------------------------------------------------------------

uint32_t DriveCellLog(const Sample& s, int32_t sizeX, int32_t sizeZ)
{
    Fnv f;
    g_events = Fnv();
    wchar_t temp[MAX_PATH];
    const DWORD length = GetTempPathW(MAX_PATH, temp);
    if (!length || length >= MAX_PATH - 40) {
        log::Write("autotest: query6: no temporary folder");
        return Part("celllog", f, "no folder");
    }
    wchar_t folder[MAX_PATH];
    std::swprintf(folder, MAX_PATH, L"%lsopensc4-q6-%lu", temp, static_cast<unsigned long>(GetCurrentProcessId()));
    CreateDirectoryW(folder, nullptr);
    wchar_t file[MAX_PATH];
    std::swprintf(file, MAX_PATH, L"%ls\\Query.txt", folder);

    std::vector<std::pair<int32_t, int32_t>> cells = {{sizeX / 2, sizeZ / 2}, {0, 0}};
    for (std::size_t i = 0; i < s.buildings.size() && i < 4; ++i)
        cells.push_back({s.buildings[i].x, s.buildings[i].z});
    for (const Building& b : s.abandoned)
        cells.push_back({b.x, b.z});
    for (std::size_t i = 0; i < s.pieceCells.size() && i < 4; ++i)
        cells.push_back(s.pieceCells[i]);
    void* const sink = Global<void*>(addr::SC4QueryTool_LogSink);
    std::size_t total = 0;
    int32_t written = 0;
    for (const auto& c : cells) {
        wchar_t kept[MAX_PATH];
        const DWORD keptLength = GetCurrentDirectoryW(MAX_PATH, kept);
        if (!keptLength || keptLength >= MAX_PATH || !SetCurrentDirectoryW(folder))
            break;
        Cdecl<void>(addr::SC4QueryTool_LogCell, c.first, c.second);
        SetCurrentDirectoryW(kept);
        std::string bytes;
        const HANDLE handle = CreateFileW(file, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                          OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle != INVALID_HANDLE_VALUE) {
            char buffer[4096];
            DWORD read = 0;
            while (ReadFile(handle, buffer, sizeof buffer, &read, nullptr) && read)
                bytes.append(buffer, read);
            CloseHandle(handle);
            DeleteFileW(file);
            ++written;
        }
        f.Add(c.first);
        f.Add(c.second);
        f.Text(bytes);
        total += bytes.size();
    }
    f.Add(uint8_t{Global<void*>(addr::SC4QueryTool_LogSink) != nullptr});
    Global<void*>(addr::SC4QueryTool_LogSink) = sink;
    RemoveDirectoryW(folder);
    char more[80];
    std::snprintf(more, sizeof more, "%d cells, %d files, %d bytes", static_cast<int32_t>(cells.size()), written,
                  static_cast<int32_t>(total));
    return Part("celllog", f, more);
}

// --- 32: the query dialog ---------------------------------------------------------------------

// While a dialog runs, the window manager's DoModalWin (slot 41) is this:
// it records what the query keeps for DoMessage and answers g_answer.
uint32_t g_answer = 1;
int32_t g_played = 0;
Fnv g_modal;
void* g_asked = nullptr;

uint32_t __fastcall ModalStandIn(void*, void*, void* window)
{
    ++g_played;
    g_modal.Add(uint8_t{window != nullptr});
    g_modal.Add(static_cast<uint32_t>(Global<std::uintptr_t>(addr::SC4QueryTool_FillCellWalk)));
    g_modal.Add(uint8_t{Global<void*>(addr::SC4QueryTool_FillCellWindow) == window});
    g_modal.Add(uint8_t{Global<void*>(addr::SC4QueryTool_FillCellQueries) == g_asked});
    g_modal.Add(uint8_t{Global<void*>(addr::SC4QueryTool_FillCellQueries + 4) != nullptr});
    return g_answer;
}

template <typename F>
bool RunModal(F run)
{
    void* const manager = Cdecl<void*>(addr::SC4_GetWinManager);
    if (!manager)
        return false;
    void** const slot = &(*static_cast<void***>(manager))[0xA4 / sizeof(void*)];
    void* const loop = *slot;
    DWORD old = 0;
    if (!VirtualProtect(slot, sizeof *slot, PAGE_READWRITE, &old))
        return false;
    *slot = reinterpret_cast<void*>(&ModalStandIn);
    run();
    *slot = loop;
    VirtualProtect(slot, sizeof *slot, old, &old);
    return true;
}

uint32_t DriveDialogs(const Sample& s)
{
    Fnv f;
    g_events = Fnv();
    g_modal = Fnv();
    g_played = 0;
    int32_t accepted = 0;
    int32_t asked = 0;
    const bool ran = RunModal([&] {
        for (const Building& b : s.buildings) {
            uint32_t instance = 0;
            void* const holder = Virtual<void*>(b.occupant, 0x18);
            if (!Cdecl<uint8_t>(addr::SC4_GetPropertyUint32, holder, kLayoutProperty, &instance))
                continue;
            f.Add(instance);
            g_asked = b.occupant;
            for (const uint32_t answer : {1u, 0u}) {
                g_answer = answer;
                const uint8_t result = Cdecl<uint8_t>(addr::SC4QueryTool_DoQueryDialog, b.occupant);
                f.Add(result);
                accepted += result;
                f.Add(static_cast<uint32_t>(Global<std::uintptr_t>(addr::SC4QueryTool_FillCellWalk)));
                f.Add(uint8_t{Global<void*>(addr::SC4QueryTool_FillCellWindow) != nullptr});
                f.Add(uint8_t{Global<void*>(addr::SC4QueryTool_FillCellQueries) != nullptr});
            }
            if (++asked >= 3)
                break;
        }
        // An occupant without a query layout: false at once.
        if (!s.pieces.empty())
            f.Add(Cdecl<uint8_t>(addr::SC4QueryTool_DoQueryDialog, s.pieces[0]));
    });
    g_asked = nullptr;
    g_answer = 1;
    f.Add(uint8_t{ran});
    f.Add(g_played);
    f.Add(g_modal.h);
    char more[80];
    std::snprintf(more, sizeof more, "%d occupants, %d dialogs run, %d accepted", asked, g_played, accepted);
    return Part("dialogs", f, more);
}

void Drive(const Day& day)
{
    if (!OnRunDay(day, "OPENSC4_TEST_Q6") || !game::IsBuild641())
        return;
    char* const view = CityView();
    if (!view || !day.city || !Global<void*>(addr::SC4_City) || !Global<void*>(addr::SC4_LotManager)) {
        log::Write("autotest: query6: no city view");
        return;
    }
    const bool commit = EnvInt("OPENSC4_TEST_Q6_COMMIT", 0) != 0;
    const int parts = EnvInt("OPENSC4_TEST_Q6_PARTS", commit ? 63 : 31) & (commit ? 63 : 31);
    SetUpFakes();
    const int32_t sizeX = static_cast<int32_t>(day.city->CellCountX());
    const int32_t sizeZ = static_cast<int32_t>(day.city->CellCountZ());
    Sample sample;
    Collect(sample, sizeX, sizeZ);
    char counts[160];
    std::snprintf(counts, sizeof counts,
                  "%d buildings, %d plants, %d pumps, %d abandoned, %d network types; parts %d",
                  static_cast<int32_t>(sample.buildings.size()), static_cast<int32_t>(sample.plants.size()),
                  static_cast<int32_t>(sample.pumps.size()), static_cast<int32_t>(sample.abandoned.size()),
                  static_cast<int32_t>(sample.pieces.size()), parts);
    log::Write("autotest: query6: %s", counts);
    Fnv total;
    total.Text(counts);
    if (parts & 1)
        total.Add(DriveFillers(sample));
    if (parts & 2)
        total.Add(DriveHelpers(sample));
    if (parts & 4) {
        Virtual<uint8_t>(view, 0x68, uint32_t{0});
        Virtual<uint8_t>(view, 0x1C, uint32_t{0x6A935CF4}, static_cast<void*>(nullptr), uint32_t{0});
        char* const tool = Virtual<char*>(view, 0x60);
        const bool ours = tool && At<std::uintptr_t>(tool, 0) == addr::SC4QueryTool_Vtable;
        total.Add(uint8_t{ours});
        if (ours) {
            Virtual<uint32_t>(tool, 0x4);
            total.Add(DriveTraffic(tool, sample));
            Virtual<uint8_t>(view, 0x68, uint32_t{0});
            total.Add(uint8_t{At<void*>(tool, field::kOccupant) != nullptr});
            total.Add(Virtual<uint32_t>(tool, 0x8));
            log::Write("autotest: query6: put away");
        } else {
            log::Write("autotest: query6: no query tool from the view");
            Virtual<uint8_t>(view, 0x68, uint32_t{0});
        }
    }
    if (parts & 8)
        total.Add(DriveWalks(sample));
    if (parts & 16)
        total.Add(DriveCellLog(sample, sizeX, sizeZ));
    if (parts & 32)
        total.Add(DriveDialogs(sample));
    for (void* const piece : sample.pieces)
        Virtual<uint32_t>(piece, 0x8);
    total.Add(g_window.references);
    total.Add(g_text.references);
    total.Add(g_picture.references);
    total.Add(g_number.references);
    std::snprintf(g_token, sizeof g_token, " query6 %08X", total.h);
    log::Write("autotest: query6: total %08X (references %d %d %d %d)", total.h, g_window.references,
               g_text.references, g_picture.references, g_number.references);
}

const DayHook kQuery6{"query6", 66, &Drive};

void Fingerprint(cISC4City*, std::string& line)
{
    line += g_token;
}

const FingerprintPart kFingerprint{"query6", 27, &Fingerprint};

} // namespace
} // namespace opensc4::autotest
