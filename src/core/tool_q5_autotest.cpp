// The query tool's part of the autotest, fifth piece (src/core/
// autotest_hooks.h; tool_query_5a.cpp, tool_query_5b.cpp): calls the
// functions of 0x004CC6E0 to 0x004D07C0 and hashes what comes out, so that a
// run with ui.tools.query5a/5b on and one with them off can be compared.
// Only the game's addresses and vtable slots are called, never our own
// functions.
//
//   OPENSC4_TEST_Q5=<day>    token "query5", sub-hashes on the log lines
//     "autotest: query5: ...". Nothing here changes the city (the texts only
//     read simulators; the windows' read-back walk reaches only fillers by
//     cell that read nothing back). OPENSC4_TEST_Q5_PARTS picks the parts
//     (default 63):
//
//   1   fillers 20 and 91 through 0x004D3A90 for the sample's buildings,
//       with fill 1 and 0, and without an occupant, on a stand-in window.
//   2   the debug texts: 0x004CCF60 for the sample's buildings; 0x004CD820,
//       0x004CE8A0 (with the zone the zone manager gives, and every zone
//       type 0 to 15 at one cell) and 0x004CF2B0 at the buildings' cells
//       and a few others.
//   4   the development log with a stand-in log object at 0x00B21850:
//       0x004CE440 without a record (its static string), 0x004CE320 and
//       0x004CE440 with a record made for the test (two attempts, a lot
//       size, twelve candidates, outcomes 27 and 99).
//   8   the tool of kind 1 as the view makes it: 0x004CF8A0 for a few
//       network pieces at +0x2C.
//   16  on that tool: 0x004CFB30 for the buildings' lots, 0x004CFD20 for
//       their boxes (mode 0 and 1), 0x004D0190 for the buildings (title,
//       text, icon, route mode).
//   32  the windows with a stand-in for the window manager's DoModalWin
//       (slot 41): 0x004CCCB0 (kinds 0xB and 0xC, answered 0 and 1) at a
//       few cells; 0x004CC6E0 for network pieces on the city's edge (only
//       those of a type the window knows) and an inside building.
//   64  (not in the default) 0x004CCCB0 with kind 0xE, which plays a sound.
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
#include <cstdarg>
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

namespace field = tool_query::field;

constexpr uint32_t kTextControl = 0x212CDC1F, kNumberControl = 0x21325207, kPictureControl = 0xC12CEA13;
constexpr uint32_t kTypeNetwork = 0xC772BF98;
constexpr uint32_t kNetworkOccupant = 0xA821EF94;
// The network types the edge window has a type for (0x004CC6E0).
constexpr uint32_t kEdgeNetworkTypes[] = {0xB, 0, 6, 0xC, 2, 7, 8, 9, 1, 4};

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
void* g_numberVtable[12];
void* g_pictureVtable[8];
void* g_partVtable[80];
void* g_sinkVtable[16];
void* g_logVtable[32];
Fake g_window, g_text, g_number, g_picture, g_part, g_sink, g_log;
Fnv g_events;
std::string g_caption;
std::string g_logText;

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

void __fastcall FakeSetCaption(Fake*, void*, const void* text)
{
    g_caption = TextOf(text);
    g_events.Add(uint8_t{0x04});
    g_events.Text(g_caption);
}

void __fastcall FakeNumberSet(Fake*, void*, int32_t value)
{
    g_events.Add(uint8_t{0x14});
    g_events.Add(value);
}

int32_t __fastcall FakeGetNumber(Fake*, void*)
{
    g_events.Add(uint8_t{0x05});
    return 0;
}

void* __fastcall FakePart(Fake*, void*)
{
    g_events.Add(uint8_t{0x03});
    return &g_part;
}

void __fastcall FakeSetImage(Fake*, void*, void* image)
{
    g_events.Add(uint8_t{0x10});
    g_events.Add(uint8_t{image != nullptr});
}

void __fastcall FakePartCall69(Fake*, void*)
{
    g_events.Add(uint8_t{0x45});
}

void __fastcall FakePartCall23(Fake*, void*)
{
    g_events.Add(uint8_t{0x17});
}

void __fastcall FakeSink(Fake*, void*, const char* text)
{
    g_logText += text ? text : "(null)";
}

void __cdecl FakeLog(void*, const char* format, ...)
{
    char text[256];
    va_list arguments;
    va_start(arguments, format);
    std::vsnprintf(text, sizeof text, format, arguments);
    va_end(arguments);
    g_events.Add(uint8_t{0x1B});
    g_events.Text(text);
}

void SetUpFakes()
{
    void* const addRef = reinterpret_cast<void*>(&FakeAddRef);
    void* const release = reinterpret_cast<void*>(&FakeRelease);
    for (void** table : {g_windowVtable, g_textVtable, g_numberVtable, g_pictureVtable, g_partVtable, g_sinkVtable,
                         g_logVtable}) {
        table[1] = addRef;
        table[2] = release;
    }
    g_windowVtable[3] = reinterpret_cast<void*>(&FakeSelf);
    g_windowVtable[11] = reinterpret_cast<void*>(&FakeParent);
    g_windowVtable[35] = reinterpret_cast<void*>(&FakeNoChild);
    g_windowVtable[37] = reinterpret_cast<void*>(&FakeGetChildAs);
    g_textVtable[4] = reinterpret_cast<void*>(&FakeSetCaption);
    g_numberVtable[3] = reinterpret_cast<void*>(&FakeSelf);
    g_numberVtable[4] = reinterpret_cast<void*>(&FakeGetNumber);
    g_numberVtable[5] = reinterpret_cast<void*>(&FakeNumberSet);
    g_pictureVtable[3] = reinterpret_cast<void*>(&FakePart);
    g_pictureVtable[4] = reinterpret_cast<void*>(&FakeSetImage);
    g_partVtable[23] = reinterpret_cast<void*>(&FakePartCall23);
    g_partVtable[69] = reinterpret_cast<void*>(&FakePartCall69);
    g_sinkVtable[14] = reinterpret_cast<void*>(&FakeSink);
    g_logVtable[27] = reinterpret_cast<void*>(&FakeLog);
    g_window = {g_windowVtable, 0};
    g_text = {g_textVtable, 0};
    g_number = {g_numberVtable, 0};
    g_picture = {g_pictureVtable, 0};
    g_part = {g_partVtable, 0};
    g_sink = {g_sinkVtable, 0};
    g_log = {g_logVtable, 0};
}

// --- The city's occupants -------------------------------------------------------------------

struct Building {
    void* lot;
    void* occupant;
    int32_t x, z;
};

struct Sample {
    std::vector<Building> buildings; // up to 24, every third cell
    std::vector<void*> networks;      // up to 3 network pieces, referenced by the test
    std::vector<void*> edges;         // up to 6 network pieces on the edge with a type the window knows, referenced
    int32_t edgeOthers = 0;           // pieces on the edge of no such type (not called)
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

bool KnownEdgeType(void* occupant)
{
    void* network = nullptr;
    if (!Virtual<uint8_t>(occupant, 0x0, kNetworkOccupant, &network))
        return false;
    bool known = false;
    for (const uint32_t type : kEdgeNetworkTypes)
        known = known || Virtual<uint8_t>(network, 0x58, type) != 0;
    Virtual<uint32_t>(network, 0x8);
    return known;
}

void Collect(Sample& out, int32_t sizeX, int32_t sizeZ)
{
    void* const lotManager = Global<void*>(addr::SC4_LotManager);
    std::vector<void*> lots;
    for (int32_t z = 0; z < sizeZ; z += 3) {
        for (int32_t x = 0; x < sizeX; x += 3) {
            void* const lot = Virtual<void*>(lotManager, 0x18, x, z, uint32_t{0});
            void* const building = lot ? Virtual<void*>(lot, 0xC0) : nullptr;
            void* const occupant = building ? Virtual<void*>(building, 0xC) : nullptr;
            if (!occupant || std::find(lots.begin(), lots.end(), lot) != lots.end())
                continue;
            lots.push_back(lot);
            if (out.buildings.size() < 24)
                out.buildings.push_back({lot, occupant, x, z});
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
    int32_t seen = 0;
    for (void** node = static_cast<void**>(end[0]); node != end; node = static_cast<void**>(node[0])) {
        void* const occupant = node[2];
        if (Virtual<uint32_t>(occupant, 0x1C) != kTypeNetwork)
            continue;
        int32_t x = 0, z = 0;
        CellOf(occupant, x, z);
        if (x == 0 || z == 0 || x == sizeX - 1 || z == sizeZ - 1) {
            if (!KnownEdgeType(occupant)) {
                ++out.edgeOthers;
            } else if (out.edges.size() < 6) {
                Virtual<uint32_t>(occupant, 0x4);
                out.edges.push_back(occupant);
            }
        }
        if (++seen % 37 != 1 || out.networks.size() >= 3)
            continue;
        Virtual<uint32_t>(occupant, 0x4);
        out.networks.push_back(occupant);
    }
    Virtual<void>(occupants, 0xC0, &list);
    This<void>(addr::SC4List_Destroy2, &list);
}

uint32_t Part(const char* name, Fnv& f, const char* more)
{
    f.Add(g_events.h);
    log::Write("autotest: query5: %s %08X (%s)", name, f.h, more);
    return f.h;
}

// --- 1: fillers 20 and 91 ---------------------------------------------------------------------

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
    for (const Building& b : s.buildings)
        for (const uint32_t index : {20u, 91u})
            for (const uint32_t value : {1u, 0u})
                fill(index, value, b.occupant);
    for (const uint32_t index : {20u, 91u})
        fill(index, 1, nullptr);
    char more[80];
    std::snprintf(more, sizeof more, "%d captions", said);
    return Part("fillers", f, more);
}

// --- 2: the debug texts -----------------------------------------------------------------------

uint32_t DriveTexts(const Sample& s, int32_t sizeX, int32_t sizeZ)
{
    Fnv f;
    g_events = Fnv();
    std::size_t longest = 0;
    const auto take = [&](const Text& text) {
        const std::string read = text.Read();
        longest = std::max(longest, read.size());
        f.Text(read);
    };
    for (const Building& b : s.buildings) {
        Text text;
        Cdecl<void>(addr::SC4QueryTool_AppendBuildingText, b.occupant, text.words);
        take(text);
    }
    std::vector<std::pair<int32_t, int32_t>> cells = {{0, 0}, {sizeX / 2, sizeZ / 3}, {sizeX - 1, sizeZ - 1}};
    for (std::size_t i = 0; i < s.buildings.size() && i < 12; ++i)
        cells.push_back({s.buildings[i].x, s.buildings[i].z});
    void* const city = Global<void*>(addr::SC4_City);
    for (const auto& c : cells) {
        Text demand;
        Cdecl<void>(addr::SC4QueryTool_AppendDemandText, c.first, c.second, demand.words);
        take(demand);
        float point[3] = {};
        Virtual<void>(city, 0x184, c.first, c.second, &point[0], &point[2]);
        const uint32_t zone = Virtual<uint32_t>(Global<void*>(addr::SC4_Service43D14), 0x30, c.first, c.second);
        f.Add(zone);
        if (zone == 14 && !Global<void*>(addr::SC4_Value43D28))
            continue;
        Text cell;
        Cdecl<void>(addr::SC4QueryTool_AppendCellText, zone, static_cast<float*>(point), cell.words);
        take(cell);
        Text shares;
        Cdecl<void>(addr::SC4QueryTool_AppendLotShares, c.first, c.second, shares.words);
        take(shares);
    }
    if (!s.buildings.empty()) {
        float point[3] = {};
        Virtual<void>(city, 0x184, s.buildings[0].x, s.buildings[0].z, &point[0], &point[2]);
        for (uint32_t zone = 0; zone <= 16; ++zone) {
            if (zone == 14 && !Global<void*>(addr::SC4_Value43D28))
                continue;
            Text cell;
            Cdecl<void>(addr::SC4QueryTool_AppendCellText, zone, static_cast<float*>(point), cell.words);
            take(cell);
        }
    }
    char more[80];
    std::snprintf(more, sizeof more, "%d cells, longest text %d", static_cast<int32_t>(cells.size()),
                  static_cast<int32_t>(longest));
    return Part("texts", f, more);
}

// --- 4: the development log -------------------------------------------------------------------

void __fastcall FakeDate(Fake*, void*, void* text, uint32_t flags)
{
    Cdecl<void>(addr::cRZBaseString_Sprintf, text, "day %d (%d)", int32_t{12}, flags);
}

uint32_t __fastcall FakeDeveloperType(Fake*, void*)
{
    return 0x1020;
}

void __fastcall FakeLotSize(Fake*, void*, int32_t* first, int32_t* second)
{
    *first = 3;
    *second = 4;
}

uint32_t DriveLog()
{
    Fnv f;
    g_events = Fnv();
    void* const kept = Global<void*>(addr::SC4QueryTool_LogSink);
    Global<void*>(addr::SC4QueryTool_LogSink) = &g_sink;
    g_logText.clear();

    // Without a record: only the static string.
    Cdecl<void>(addr::SC4QueryTool_LogDevelopmentAttempt, static_cast<void*>(nullptr));
    f.Add(Global<uint32_t>(addr::SC4QueryTool_UnknownBuildingGuard) & 1u);
    f.Text(TextOf(reinterpret_cast<void*>(addr::SC4QueryTool_UnknownBuilding)));

    // A record made for the test.
    using namespace tool_query;
    static void* dateVtable[4];
    static void* developerVtable[8];
    static void* sizeVtable[8];
    dateVtable[2] = reinterpret_cast<void*>(&FakeDate);
    developerVtable[6] = reinterpret_cast<void*>(&FakeDeveloperType);
    sizeVtable[7] = reinterpret_cast<void*>(&FakeLotSize);
    Fake developer = {developerVtable, 0};
    Fake size = {sizeVtable, 0};
    uint32_t counts[development_record::kFailureRows][kOutcomeCount] = {};
    uint32_t* rows[development_record::kFailureRows][3] = {};
    for (std::size_t row = 0; row < development_record::kFailureRows; ++row) {
        for (uint32_t outcome = 0; outcome < kOutcomeCount; ++outcome)
            counts[row][outcome] = (row * 7 + outcome * 3) % 5 == 0 ? static_cast<uint32_t>(row + outcome) : 0;
        rows[row][0] = counts[row];
        rows[row][1] = counts[row] + kOutcomeCount;
        rows[row][2] = rows[row][1];
    }
    // The lot size tree: a header and one node.
    char header[0x18] = {};
    char sizeNode[0x18] = {};
    At<char*>(header, 0x4) = sizeNode;
    At<char*>(header, 0x8) = sizeNode;
    At<char*>(header, 0xC) = sizeNode;
    At<char*>(sizeNode, 0x4) = header;
    At<void*>(sizeNode, 0x10) = &size;
    char emptyHeader[0x18] = {};
    At<char*>(emptyHeader, 0x8) = emptyHeader;
    At<char*>(emptyHeader, 0xC) = emptyHeader;
    char candidates[12][0x38] = {};
    char* candidatePointers[12] = {};
    for (int i = 0; i < 12; ++i) {
        char* const c = candidates[i];
        At<uint32_t>(c, development_candidate::kKind) = static_cast<uint32_t>(i % 5 + 1);
        At<int32_t>(c, development_candidate::kX) = 10 + i;
        At<int32_t>(c, development_candidate::kZ) = 20 - i;
        At<uint8_t>(c, development_candidate::kSizeX) = static_cast<uint8_t>(1 + i % 4);
        At<uint8_t>(c, development_candidate::kSizeZ) = static_cast<uint8_t>(1 + i % 3);
        At<int32_t>(c, development_candidate::kCapacity) = 100 * i;
        At<int32_t>(c, development_candidate::kAbandoned) = i;
        At<float>(c, development_candidate::kScore) = 0.25f * static_cast<float>(i);
        candidatePointers[i] = c;
    }
    char sentinel[0x8] = {};
    char attempts[2][0x40] = {};
    At<char*>(sentinel, 0x0) = attempts[0];
    At<char*>(sentinel, 0x4) = attempts[1];
    At<char*>(attempts[0], 0x0) = attempts[1];
    At<char*>(attempts[0], 0x4) = sentinel;
    At<char*>(attempts[1], 0x0) = sentinel;
    At<char*>(attempts[1], 0x4) = attempts[0];
    At<uint8_t>(attempts[0], development_attempt::kNotFussy) = 1;
    At<char*>(attempts[0], development_attempt::kLotSizes) = header;
    At<char**>(attempts[0], development_attempt::kCandidates) = candidatePointers;
    At<char**>(attempts[0], development_attempt::kCandidatesEnd) = candidatePointers + 12;
    At<int32_t>(attempts[0], development_attempt::kWantMin) = 5;
    At<int32_t>(attempts[0], development_attempt::kWantMax) = 50;
    At<uint32_t>(attempts[0], development_attempt::kOutcome) = 27;
    At<char*>(attempts[1], development_attempt::kLotSizes) = emptyHeader;
    At<char**>(attempts[1], development_attempt::kCandidates) = candidatePointers;
    At<char**>(attempts[1], development_attempt::kCandidatesEnd) = candidatePointers + 3;
    At<uint32_t>(attempts[1], development_attempt::kOutcome) = 99;
    Fake date = {dateVtable, 0};
    char record[0x40] = {};
    std::memcpy(record, &date, sizeof date);
    At<void*>(record, development_record::kDeveloper) = &developer;
    At<int32_t>(record, development_record::kTractX) = 7;
    At<int32_t>(record, development_record::kTractZ) = 9;
    At<int32_t>(record, development_record::kPopulation) = 1234;
    At<int32_t>(record, development_record::kCapacity) = 1500;
    At<int32_t>(record, development_record::kMaxCapacity) = 2000;
    At<int32_t>(record, development_record::kDesirability) = 77;
    At<int32_t>(record, development_record::kResources) = 300;
    At<char*>(record, development_record::kAttempts) = sentinel;
    At<void*>(record, development_record::kFailures) = rows;

    Cdecl<void>(addr::SC4QueryTool_LogDevelopmentFailures, static_cast<void*>(record));
    const std::size_t table = g_logText.size();
    Cdecl<void>(addr::SC4QueryTool_LogDevelopmentAttempt, static_cast<void*>(record));
    Global<void*>(addr::SC4QueryTool_LogSink) = kept;
    f.Text(g_logText);
    char more[80];
    std::snprintf(more, sizeof more, "log %d characters (table %d)", static_cast<int32_t>(g_logText.size()),
                  static_cast<int32_t>(table));
    return Part("log", f, more);
}

// --- 8, 16: on the tool -----------------------------------------------------------------------

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

uint32_t DriveNames(char* tool, const Sample& s)
{
    Fnv f;
    g_events = Fnv();
    std::string names;
    std::vector<void*> pieces = s.networks;
    pieces.insert(pieces.end(), s.edges.begin(), s.edges.end());
    for (void* const piece : pieces) {
        SetOccupant(tool, piece);
        Text text;
        This<void>(addr::SC4QueryTool_NetworkName, tool, text.words, static_cast<void*>(&g_log));
        const std::string read = text.Read();
        f.Text(read);
        names += read.empty() ? std::string("-") : read;
        names += "; ";
    }
    if (!s.buildings.empty()) {
        SetOccupant(tool, s.buildings[0].occupant);
        Text text;
        This<void>(addr::SC4QueryTool_NetworkName, tool, text.words, static_cast<void*>(&g_log));
        f.Text(text.Read());
    }
    if (names.size() > 200)
        names.resize(200);
    return Part("names", f, names.c_str());
}

uint32_t DriveRoutes(char* tool, const Sample& s)
{
    Fnv f;
    g_events = Fnv();
    int32_t titled = 0;
    for (std::size_t i = 0; i < s.buildings.size(); ++i) {
        const Building& b = s.buildings[i];
        if (i < 8) {
            Text counts;
            reinterpret_cast<void(__stdcall*)(void*, void*)>(addr::SC4QueryTool_LotRouteCounts)(b.lot, counts.words);
            f.Text(counts.Read());
            int32_t box[4] = {};
            Virtual<uint8_t>(b.lot, 0x38, static_cast<int32_t*>(box));
            f.Bytes(box, sizeof box);
            for (const uint32_t mode : {0u, 1u}) {
                Text boxed;
                This<void>(addr::SC4QueryTool_BoxRouteCounts, tool, static_cast<int32_t*>(box), boxed.words, mode);
                f.Text(boxed.Read());
            }
        }
        SetOccupant(tool, b.occupant);
        At<uint32_t>(tool, field::kRouteMode) = 3;
        Text title, text;
        uint32_t icon = 0x12345678;
        This<void>(addr::SC4QueryTool_RouteSummary, tool, title.words, text.words, &icon);
        f.Text(title.Read());
        f.Text(text.Read());
        f.Add(icon);
        f.Add(At<uint32_t>(tool, field::kRouteMode));
        titled += !title.Read().empty();
    }
    char more[80];
    std::snprintf(more, sizeof more, "%d titled of %d", titled, static_cast<int32_t>(s.buildings.size()));
    return Part("routes", f, more);
}

// --- 32: the windows --------------------------------------------------------------------------

// While a window runs, the window manager's DoModalWin (slot 41) is this:
// it records what the query keeps for DoMessage and answers g_answer.
uint32_t g_answer = 0;
int32_t g_played = 0;
Fnv g_modal;

uint32_t __fastcall ModalStandIn(void*, void*, void* window)
{
    ++g_played;
    g_modal.Add(uint8_t{window != nullptr});
    const std::uintptr_t walk = Global<std::uintptr_t>(addr::SC4QueryTool_FillCellWalk);
    g_modal.Add(uint8_t{walk == addr::SC4QueryTool_FillCellChildren});
    g_modal.Add(uint8_t{Global<void*>(addr::SC4QueryTool_FillCellWindow) == window});
    for (int i = 0; i < 2; ++i) {
        const int32_t* const query = Global<int32_t*>(addr::SC4QueryTool_FillCellQueries + 4 * i);
        g_modal.Add(uint8_t{query != nullptr});
        if (query)
            g_modal.Bytes(query, 0x10);
    }
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

void AfterWindow(Fnv& f)
{
    f.Add(Global<std::uintptr_t>(addr::SC4QueryTool_FillCellWalk));
    f.Add(uint8_t{Global<void*>(addr::SC4QueryTool_FillCellWindow) != nullptr});
    f.Add(uint8_t{Global<void*>(addr::SC4QueryTool_FillCellQueries) != nullptr});
    f.Add(uint8_t{Global<void*>(addr::SC4QueryTool_FillCellQueries + 4) != nullptr});
}

uint32_t DriveWindows(const Sample& s, int32_t sizeX, int32_t sizeZ, bool sound)
{
    Fnv f;
    g_events = Fnv();
    g_modal = Fnv();
    g_played = 0;
    int32_t accepted = 0;
    const bool ran = RunModal([&] {
        std::vector<std::pair<int32_t, int32_t>> cells = {{0, sizeZ / 2}, {sizeX / 2, sizeZ / 2}};
        if (!s.buildings.empty())
            cells.push_back({s.buildings[0].x, s.buildings[0].z});
        std::vector<int32_t> kinds = {0xB, 0xC};
        if (sound)
            kinds.push_back(0xE);
        for (const auto& c : cells) {
            for (const int32_t kind : kinds) {
                for (const uint32_t answer : {0u, 1u}) {
                    g_answer = answer;
                    const uint8_t result = Cdecl<uint8_t>(addr::SC4QueryTool_RunCellWindow, kind, c.first, c.second);
                    f.Add(result);
                    accepted += result;
                    AfterWindow(f);
                }
            }
        }
        g_answer = 0;
        for (void* const piece : s.edges) {
            const uint8_t result = Cdecl<uint8_t>(addr::SC4QueryTool_RunEdgeWindow, piece);
            f.Add(result);
            accepted += result;
            AfterWindow(f);
        }
        // An inside building only: on the edge, an occupant of neither
        // interface would give the game's window a type from its stack.
        for (const Building& b : s.buildings) {
            int32_t x = 0, z = 0;
            CellOf(b.occupant, x, z);
            if (x <= 0 || z <= 0 || x >= sizeX - 1 || z >= sizeZ - 1)
                continue;
            f.Add(Cdecl<uint8_t>(addr::SC4QueryTool_RunEdgeWindow, b.occupant));
            AfterWindow(f);
            break;
        }
    });
    f.Add(uint8_t{ran});
    f.Add(g_played);
    f.Add(g_modal.h);
    char more[120];
    std::snprintf(more, sizeof more, "%d windows run, %d accepted, %d edge pieces (%d of other types)", g_played,
                  accepted, static_cast<int32_t>(s.edges.size()), s.edgeOthers);
    return Part("windows", f, more);
}

void Drive(const Day& day)
{
    if (!OnRunDay(day, "OPENSC4_TEST_Q5") || !game::IsBuild641())
        return;
    char* const view = CityView();
    if (!view || !day.city || !Global<void*>(addr::SC4_City) || !Global<void*>(addr::SC4_LotManager)) {
        log::Write("autotest: query5: no city view");
        return;
    }
    const int parts = EnvInt("OPENSC4_TEST_Q5_PARTS", 63);
    SetUpFakes();
    const int32_t sizeX = static_cast<int32_t>(day.city->CellCountX());
    const int32_t sizeZ = static_cast<int32_t>(day.city->CellCountZ());
    Sample sample;
    Collect(sample, sizeX, sizeZ);
    char counts[160];
    std::snprintf(counts, sizeof counts, "%d buildings, %d network pieces, %d edge pieces (%d other)",
                  static_cast<int32_t>(sample.buildings.size()), static_cast<int32_t>(sample.networks.size()),
                  static_cast<int32_t>(sample.edges.size()), sample.edgeOthers);
    log::Write("autotest: query5: %s", counts);
    Fnv total;
    total.Text(counts);
    if (parts & 1)
        total.Add(DriveFillers(sample));
    if (parts & 2)
        total.Add(DriveTexts(sample, sizeX, sizeZ));
    if (parts & 4)
        total.Add(DriveLog());
    if (parts & (8 | 16)) {
        Virtual<uint8_t>(view, 0x68, uint32_t{0});
        Virtual<uint8_t>(view, 0x1C, uint32_t{0x6A935CF4}, static_cast<void*>(nullptr), uint32_t{0});
        char* const tool = Virtual<char*>(view, 0x60);
        const bool ours = tool && At<std::uintptr_t>(tool, 0) == addr::SC4QueryTool_Vtable;
        total.Add(uint8_t{ours});
        if (ours) {
            Virtual<uint32_t>(tool, 0x4);
            if (parts & 8)
                total.Add(DriveNames(tool, sample));
            if (parts & 16)
                total.Add(DriveRoutes(tool, sample));
            Virtual<uint8_t>(view, 0x68, uint32_t{0});
            total.Add(uint8_t{At<void*>(tool, field::kOccupant) != nullptr});
            total.Add(Virtual<uint32_t>(tool, 0x8));
            log::Write("autotest: query5: put away");
        } else {
            log::Write("autotest: query5: no query tool from the view");
            Virtual<uint8_t>(view, 0x68, uint32_t{0});
        }
    }
    if (parts & (32 | 64))
        total.Add(DriveWindows(sample, sizeX, sizeZ, (parts & 64) != 0));
    for (void* const network : sample.networks)
        Virtual<uint32_t>(network, 0x8);
    for (void* const edge : sample.edges)
        Virtual<uint32_t>(edge, 0x8);
    total.Add(g_window.references);
    total.Add(g_text.references);
    total.Add(g_number.references);
    total.Add(g_picture.references);
    std::snprintf(g_token, sizeof g_token, " query5 %08X", total.h);
    log::Write("autotest: query5: total %08X (references %d %d %d %d)", total.h, g_window.references,
               g_text.references, g_number.references, g_picture.references);
}

const DayHook kQuery5{"query5", 65, &Drive};

void Fingerprint(cISC4City*, std::string& line)
{
    line += g_token;
}

const FingerprintPart kFingerprint{"query5", 26, &Fingerprint};

} // namespace
} // namespace opensc4::autotest
