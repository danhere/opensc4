// The query tool's part of the autotest, seventh piece (src/core/
// autotest_hooks.h; tool_query_7a.cpp, tool_query_7b.cpp): calls the
// functions of 0x004D4140 to 0x004D7680 and hashes what comes out, so that a
// run with ui.tools.query7a/7b on and one with them off can be compared.
// Only the game's addresses and vtable slots are called, never our own
// functions.
//
//   OPENSC4_TEST_Q7=<day>    token "query7", sub-hashes on the log lines
//     "autotest: query7: ...". OPENSC4_TEST_Q7_PARTS picks the parts
//     (default 7; 15 with OPENSC4_TEST_Q7_COMMIT=1; 16 only when asked):
//
//   1   DoMessage (0x004D7160) on a tool of kind 0 and one of kind 1, as the
//       view makes them: the tip message 0x0A456D95 for no occupant (with
//       and without +0x88, at a few places), the sample's buildings, one
//       network piece per type, flora, props, a power pole and a landfill,
//       each with +0x94 off and on; then 0x0A456D95 with +0x78 clear,
//       0x0A456D96, 0x26D31EC2, 0x66956814 (a stand-in window and queries
//       in 0x00B21840..0x00B2184C; it acts only on every tenth day, logged),
//       0x69247DC7 (kind 1, after the routes 0x004CAC50 made a line),
//       0xA6B79602 (the window manager's slot 33 answering fixed points) and
//       a message of no meaning. The view's slots 40 and 41 record (they do
//       nothing meanwhile); the tip's title, text and pictures are read back
//       from the info tip.
//   2   ui.tools.query7b's functions called directly on the kind 0 tool:
//       0x004D5E30 (a building's tip: title, text, two pictures, share,
//       the outs preset to sentinels) and 0x004D5C90 (the texts of the
//       building's lot) for every building of a lot (up to 300),
//       0x004D6C70 (the tip of a cell) at the sample's cells, the pieces'
//       cells, the middle, 0/0 and every seventh cell.
//   4   OnMouseDownL (slot 16) on the kind 1 tool: no modifier for
//       buildings, network pieces and nothing at +0x2C (the routes), alt
//       (nothing more), control with the view's slot 36 answering fixed
//       points (the residential simulator's slot 59, a stub); OnMouseUpL
//       (slot 18) after each.
//   8   (only with OPENSC4_TEST_Q7_COMMIT=1) DescribePick (0x004D4140)
//       over a grid of screen points with a stand-in for the window
//       manager's DoModalWin (slot 41) answering 1, with the debug byte 0
//       and 1 (the cell log then writes Query.txt into a folder of the
//       test's own, made the current directory for the call; its bytes
//       hashed, file and folder deleted); OnMouseDownL of a kind 0 tool at
//       the same points (the game's static strings 0x00B2185C, 0x00B21870,
//       0x00B21884 hashed after); DescribePick at the first five points
//       again answering 0. The query dialog's fill walk and the answer 0's
//       read back change the city (as Q6's dialogs): compare A and B commit
//       legs with each other only.
//   16  (only when asked) shift clicks of the kind 1 tool over the grid: an
//       automaton found goes to the vehicle manager (frame driven: which one
//       is found differs run to run), else the message 0x2ACD2CF3 is sent to
//       all; a group prop found goes to the traffic network map. Only how
//       many calls were made is hashed.
//   The tools are put away by the view (slot 0x68: Shutdown) after each part.
//
// Kept out of the hashes: every pointer (only whether it is set), the
// mouse (never read: slot 33 is the stand-in's), the info tip's position.
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

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <string>
#include <utility>
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
namespace message_id = tool_query::message;

constexpr uint32_t kTypeNetwork = 0xC772BF98, kTypeFlora = 0x74758926;
constexpr uint32_t kTypeProp = 0xA823821E, kTypePowerPole = 0x2890D4DE, kTypeLandfill = 0x6A11BD2B;
constexpr uint32_t kNetworkOccupant = 0xA821EF94;
constexpr uint32_t kMakeFirstTool = 0x6A935CF3;  // the view's command for the tool of kind 0
constexpr uint32_t kMakeSecondTool = 0x6A935CF4; // and of kind 1
constexpr float kPlaceHeight = 270.0f;           // +0x80 of the places the test sets

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

// Any cIGZString through its slots 6 (Data) and 7 (Strlen).
std::string StringOf(void* text)
{
    if (!text)
        return "(null)";
    const char* const data = Virtual<const char*>(text, 0x18);
    const uint32_t length = Virtual<uint32_t>(text, 0x1C);
    return data ? std::string(data, length) : std::string("?");
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
    void* Get()
    {
        return static_cast<void*>(words);
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

// --- Slot patches and stand-ins ----------------------------------------------------------------

// One vtable word replaced for as long as the patch lives.
class SlotPatch {
public:
    SlotPatch(void* object, std::size_t offset, void* ours)
    {
        if (!object)
            return;
        void** const slot = &(*static_cast<void***>(object))[offset / sizeof(void*)];
        if (!VirtualProtect(slot, sizeof *slot, PAGE_READWRITE, &protect_))
            return;
        slot_ = slot;
        old_ = *slot;
        *slot = ours;
    }
    ~SlotPatch()
    {
        if (!slot_)
            return;
        *slot_ = old_;
        VirtualProtect(slot_, sizeof *slot_, protect_, &protect_);
    }
    SlotPatch(const SlotPatch&) = delete;
    SlotPatch& operator=(const SlotPatch&) = delete;
    bool Ok() const
    {
        return slot_ != nullptr;
    }

private:
    void** slot_ = nullptr;
    void* old_ = nullptr;
    DWORD protect_ = 0;
};

Fnv g_events;
int32_t g_viewTexts = 0;
float g_point[3] = {0.0f, 0.0f, 0.0f};
int32_t g_cursor[2] = {0, 0};

// The view's slot 40 (the text at the pointer: ID, 0, title, text, 0).
void __fastcall ViewText(void*, void*, uint32_t id, uint32_t a, void* title, void* text, uint32_t b)
{
    ++g_viewTexts;
    g_events.Add(uint8_t{0x40});
    g_events.Add(id);
    g_events.Add(a);
    g_events.Text(StringOf(title));
    g_events.Text(StringOf(text));
    g_events.Add(b);
}

// The view's slot 41 (that text cleared).
void __fastcall ViewClear(void*, void*, uint32_t id)
{
    g_events.Add(uint8_t{0x41});
    g_events.Add(id);
}

// The view's slot 36 (the ground point under a screen point): g_point.
uint8_t __fastcall ViewPick(void*, void*, int32_t x, int32_t z, float* out, uint32_t flag)
{
    g_events.Add(uint8_t{0x36});
    g_events.Add(x);
    g_events.Add(z);
    g_events.Add(flag);
    out[0] = g_point[0];
    out[1] = g_point[1];
    out[2] = g_point[2];
    return 1;
}

// The window manager's slot 33 (the mouse position): g_cursor.
uint8_t __fastcall CursorAt(void*, void*, int32_t* x, int32_t* z)
{
    g_events.Add(uint8_t{0x33});
    *x = g_cursor[0];
    *z = g_cursor[1];
    return 1;
}

// A stand-in window for the day message: its slot 32 records.
struct Fake {
    void** vtable;
    int32_t references;
};
void* g_windowVtable[40];
Fake g_window;
int32_t g_queryA = 0, g_queryB = 0;

uint32_t __fastcall FakeAddRef(Fake* self, void*)
{
    return static_cast<uint32_t>(++self->references);
}

uint32_t __fastcall FakeRelease(Fake* self, void*)
{
    return static_cast<uint32_t>(--self->references);
}

uint8_t __fastcall FakeForEachChild(Fake*, void*, uint32_t iid, std::uintptr_t walk, void* value)
{
    g_events.Add(uint8_t{0x20});
    g_events.Add(iid);
    g_events.Add(static_cast<uint32_t>(walk));
    g_events.Add(static_cast<uint8_t>(value == &g_queryA ? 1 : value == &g_queryB ? 2 : 0));
    return 1;
}

void SetUpFakes()
{
    g_windowVtable[1] = reinterpret_cast<void*>(&FakeAddRef);
    g_windowVtable[2] = reinterpret_cast<void*>(&FakeRelease);
    g_windowVtable[32] = reinterpret_cast<void*>(&FakeForEachChild);
    g_window = {g_windowVtable, 0};
}

// While DescribePick runs, the window manager's DoModalWin (slot 41) is
// this: it records what the query keeps for DoMessage and answers g_answer.
uint32_t g_answer = 1;
int32_t g_played = 0;
Fnv g_modal;

uint32_t __fastcall ModalStandIn(void*, void*, void* window)
{
    ++g_played;
    g_modal.Add(uint8_t{window != nullptr});
    g_modal.Add(static_cast<uint32_t>(Global<std::uintptr_t>(addr::SC4QueryTool_FillCellWalk)));
    g_modal.Add(uint8_t{Global<void*>(addr::SC4QueryTool_FillCellWindow) == window});
    g_modal.Add(uint8_t{Global<void*>(addr::SC4QueryTool_FillCellQueries) != nullptr});
    g_modal.Add(uint8_t{Global<void*>(addr::SC4QueryTool_FillCellQueries + 4) != nullptr});
    return g_answer;
}

// --- The city's occupants -------------------------------------------------------------------

struct Building {
    void* lot;
    void* occupant;
    int32_t x, z;
};

struct Thing {
    void* occupant; // referenced
    uint32_t type;
    int32_t x, z;
};

struct Sample {
    std::vector<Building> buildings; // up to 8, every third cell
    std::vector<Building> every;     // every building of a lot, up to 300 (part 2)
    std::vector<Thing> pieces;       // one network piece per network type 0 to 12 the city has
    std::vector<Thing> others;       // up to 2 flora, 2 props, a power pole, a landfill
};

void CellOf(void* occupant, int32_t& x, int32_t& z)
{
    float position[3];
    Virtual<void>(occupant, 0x20, static_cast<float*>(position));
    x = 0;
    z = 0;
    Virtual<uint8_t>(Global<void*>(addr::SC4_City), 0x17C, position[0], position[2], &x, &z);
}

void Collect(Sample& out, int32_t sizeX, int32_t sizeZ)
{
    void* const lotManager = Global<void*>(addr::SC4_LotManager);
    std::vector<void*> lots;
    for (int32_t z = 0; z < sizeZ && out.every.size() < 300; ++z) {
        for (int32_t x = 0; x < sizeX && out.every.size() < 300; ++x) {
            void* const lot = Virtual<void*>(lotManager, 0x18, x, z, uint32_t{0});
            void* const building = lot ? Virtual<void*>(lot, 0xC0) : nullptr;
            void* const occupant = building ? Virtual<void*>(building, 0xC) : nullptr;
            bool seen = false;
            for (void* const known : lots)
                seen = seen || known == lot;
            if (!occupant || seen)
                continue;
            lots.push_back(lot);
            const Building b{lot, occupant, x, z};
            out.every.push_back(b);
            if (x % 3 == 0 && z % 3 == 0 && out.buildings.size() < 8)
                out.buildings.push_back(b);
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
    int32_t flora = 0, props = 0, poles = 0, landfills = 0;
    for (void** node = static_cast<void**>(end[0]); node != end; node = static_cast<void**>(node[0])) {
        void* const occupant = node[2];
        const uint32_t type = Virtual<uint32_t>(occupant, 0x1C);
        int32_t* count = nullptr;
        int32_t most = 0;
        if (type == kTypeFlora) {
            count = &flora;
            most = 2;
        } else if (type == kTypeProp) {
            count = &props;
            most = 2;
        } else if (type == kTypePowerPole) {
            count = &poles;
            most = 1;
        } else if (type == kTypeLandfill) {
            count = &landfills;
            most = 1;
        }
        if (count && *count < most) {
            ++*count;
            Thing thing{occupant, type, 0, 0};
            Virtual<uint32_t>(occupant, 0x4);
            CellOf(occupant, thing.x, thing.z);
            out.others.push_back(thing);
            continue;
        }
        if (type != kTypeNetwork)
            continue;
        void* network = nullptr;
        if (!Virtual<uint8_t>(occupant, 0x0, kNetworkOccupant, &network))
            continue;
        for (uint32_t kind = 0; kind < 13; ++kind) {
            if (had[kind] || !Virtual<uint8_t>(network, 0x58, kind))
                continue;
            had[kind] = true;
            Thing thing{occupant, type, 0, 0};
            Virtual<uint32_t>(occupant, 0x4);
            CellOf(occupant, thing.x, thing.z);
            out.pieces.push_back(thing);
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
    log::Write("autotest: query7: %s %08X (%s)", name, f.h, more);
    return f.h;
}

// --- The tools -----------------------------------------------------------------------------

// The tool the view makes for one of its two commands (as the view's
// command handler does), referenced; null when the view did not make the
// query tool.
char* MakeTool(char* view, uint32_t command)
{
    Virtual<uint8_t>(view, 0x68, uint32_t{0});
    Virtual<uint8_t>(view, 0x1C, command, static_cast<void*>(nullptr), uint32_t{0});
    char* const tool = Virtual<char*>(view, 0x60);
    if (!tool || At<std::uintptr_t>(tool, 0) != addr::SC4QueryTool_Vtable)
        return nullptr;
    Virtual<uint32_t>(tool, 0x4);
    return tool;
}

// The tool put away (the view's slot 0x68: its Shutdown) and let go.
void PutAway(char* view, char* tool, Fnv& total)
{
    Virtual<uint8_t>(view, 0x68, uint32_t{0});
    total.Add(uint8_t{At<void*>(tool, field::kOccupant) != nullptr});
    total.Add(Virtual<uint32_t>(tool, 0x8));
}

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

// The place at +0x7C: the middle of a cell, at a fixed height.
void SetPlace(char* tool, int32_t x, int32_t z)
{
    At<float>(tool, field::kPlace) = static_cast<float>(x) * 16.0f + 8.0f;
    At<float>(tool, field::kPlace + 4) = kPlaceHeight;
    At<float>(tool, field::kPlace + 8) = static_cast<float>(z) * 16.0f + 8.0f;
}

// The fields DoMessage, OnMouseDownL and OnMouseMove leave.
void HashTool(Fnv& f, char* tool)
{
    void* const occupant = At<void*>(tool, field::kOccupant);
    f.Add(uint8_t{occupant != nullptr});
    if (occupant)
        f.Add(Virtual<uint32_t>(occupant, 0x1C));
    f.Bytes(tool + field::kPlace, 0xC);
    f.Add(At<uint8_t>(tool, field::kFlag78));
    f.Add(At<uint8_t>(tool, field::kFlag88));
    f.Add(At<uint8_t>(tool, field::kPressed));
    f.Add(At<uint32_t>(tool, field::kRouteMode));
    f.Bytes(tool + field::kBox, 0x10);
}

// The route line: whether there is one and how many points it holds.
void HashLine(Fnv& f, char* tool)
{
    char* const line = At<char*>(tool, field::kObject9C);
    f.Add(uint8_t{line != nullptr});
    if (!line)
        return;
    void** const sentinel = At<void**>(line, tool_query::route_line::kPoints);
    int32_t points = 0;
    for (void** node = static_cast<void**>(sentinel[0]); node != sentinel && points < 1000000;
         node = static_cast<void**>(node[0]))
        ++points;
    f.Add(points);
}

// The info tip back to nothing (title and text empty, no pictures).
void ResetTip(char* tool)
{
    void* const tip = At<void*>(tool, field::kTip);
    if (!tip)
        return;
    Text empty;
    Virtual<void>(tip, 0x18, empty.Get());
    Virtual<void>(tip, 0x20, empty.Get());
    Virtual<void>(tip, 0x10, static_cast<void*>(nullptr), uint32_t{0}, uint32_t{0});
    Virtual<void>(tip, 0x38, static_cast<void*>(nullptr), uint32_t{0}, 0.0f);
}

// What the info tip holds: title (slot 7), text (slot 9), whether it has a
// background (slot 5) and a meter picture (+0x140), the meter's share
// (+0x1A8).
void HashTip(Fnv& f, char* tool)
{
    void* const tip = At<void*>(tool, field::kTip);
    f.Add(uint8_t{tip != nullptr});
    if (!tip)
        return;
    f.Text(StringOf(Virtual<void*>(tip, 0x1C)));
    f.Text(StringOf(Virtual<void*>(tip, 0x24)));
    f.Add(uint8_t{Virtual<void*>(tip, 0x14) != nullptr});
    f.Add(uint8_t{At<void*>(tip, 0x140) != nullptr});
    f.Add(At<uint32_t>(tip, 0x1A8));
}

void HideTip(char* tool)
{
    if (void* const tip = At<void*>(tool, field::kTip))
        Virtual<void>(Virtual<void*>(tip, 0xC), 0x118);
}

// A message of the game's (cGZMessage) of a type, referenced.
void* MakeMessage(uint32_t type)
{
    void* const memory = Cdecl<void*>(addr::RZ_operator_new, uint32_t{0x2C});
    void* const message = This<void*>(addr::cGZMessage_cGZMessage, memory);
    Virtual<uint32_t>(message, 0x4);
    Virtual<void>(message, 0x14, type);
    return message;
}

// DoMessage of the tool with a message of the type.
uint8_t Send(char* tool, uint32_t type)
{
    void* const message = MakeMessage(type);
    const uint8_t answer = This<uint8_t>(addr::SC4QueryTool_DoMessage, tool + field::kTarget, message);
    Virtual<uint32_t>(message, 0x8);
    return answer;
}

// --- 1: DoMessage --------------------------------------------------------------------------

uint32_t DriveMessages(char* tool, const Sample& s, int32_t sizeX, int32_t sizeZ)
{
    Fnv f;
    g_events = Fnv();
    g_viewTexts = 0;
    const uint32_t kind = At<uint32_t>(tool, field::kKind);
    const uint8_t keptFlag94 = At<uint8_t>(tool, field::kFlag94);
    int32_t tips = 0;
    {
        void* const view = At<void*>(tool, base::kView);
        SlotPatch text(view, 0xA0, reinterpret_cast<void*>(&ViewText));
        SlotPatch clear(view, 0xA4, reinterpret_cast<void*>(&ViewClear));
        f.Add(uint8_t{text.Ok() && clear.Ok()});

        const auto tip = [&](void* occupant, uint8_t picked, int32_t x, int32_t z) {
            for (const uint8_t debug : {uint8_t{0}, uint8_t{1}}) {
                SetOccupant(tool, occupant);
                SetPlace(tool, x, z);
                At<uint8_t>(tool, field::kFlag88) = picked;
                At<uint8_t>(tool, field::kFlag94) = debug;
                At<uint8_t>(tool, field::kFlag78) = 1;
                ResetTip(tool);
                f.Add(Send(tool, message_id::kTipTimer));
                HashTip(f, tool);
                ++tips;
            }
        };
        // Nothing at +0x2C: with and without a pick, at a few places.
        std::vector<std::pair<int32_t, int32_t>> places = {{sizeX / 2, sizeZ / 2}, {0, 0}};
        for (std::size_t i = 0; i < s.buildings.size() && i < 2; ++i)
            places.push_back({s.buildings[i].x, s.buildings[i].z});
        for (std::size_t i = 0; i < s.pieces.size() && i < 2; ++i)
            places.push_back({s.pieces[i].x, s.pieces[i].z});
        for (const auto& p : places) {
            tip(nullptr, 0, p.first, p.second);
            tip(nullptr, 1, p.first, p.second);
        }
        for (std::size_t i = 0; i < s.buildings.size() && i < 6; ++i)
            tip(s.buildings[i].occupant, 1, s.buildings[i].x, s.buildings[i].z);
        for (const Thing& t : s.pieces)
            tip(t.occupant, 1, t.x, t.z);
        for (const Thing& t : s.others)
            tip(t.occupant, 1, t.x, t.z);

        // The tip timer with +0x78 clear: nothing.
        At<uint8_t>(tool, field::kFlag94) = 1;
        At<uint8_t>(tool, field::kFlag78) = 0;
        ResetTip(tool);
        f.Add(Send(tool, message_id::kTipTimer));
        HashTip(f, tool);

        SetOccupant(tool, nullptr);
        f.Add(Send(tool, message_id::kMarkerTimer));
        At<uint8_t>(tool, field::kFlag78) = 1;
        f.Add(Send(tool, message_id::kTipOff));
        f.Add(At<uint8_t>(tool, field::kFlag78));
        f.Add(Send(tool, 0x12345678));
        HashTool(f, tool);
    }

    // The day: a stand-in window with two queries, one, and none.
    void* const clock = Virtual<void*>(Global<void*>(addr::SC4_SimulatorService), 0x1C);
    const uint32_t today = Virtual<uint32_t>(clock, 0x68);
    {
        void* const keptWindow = Global<void*>(addr::SC4QueryTool_FillCellWindow);
        const std::uintptr_t keptWalk = Global<std::uintptr_t>(addr::SC4QueryTool_FillCellWalk);
        void* const keptA = Global<void*>(addr::SC4QueryTool_FillCellQueries);
        void* const keptB = Global<void*>(addr::SC4QueryTool_FillCellQueries + 4);
        Global<void*>(addr::SC4QueryTool_FillCellWindow) = &g_window;
        Global<std::uintptr_t>(addr::SC4QueryTool_FillCellWalk) = 0x7A11;
        const std::pair<void*, void*> queries[] = {{&g_queryA, &g_queryB}, {nullptr, &g_queryB}, {nullptr, nullptr}};
        for (const auto& q : queries) {
            Global<void*>(addr::SC4QueryTool_FillCellQueries) = q.first;
            Global<void*>(addr::SC4QueryTool_FillCellQueries + 4) = q.second;
            f.Add(Send(tool, message_id::kDay));
        }
        Global<void*>(addr::SC4QueryTool_FillCellWindow) = keptWindow;
        Global<std::uintptr_t>(addr::SC4QueryTool_FillCellWalk) = keptWalk;
        Global<void*>(addr::SC4QueryTool_FillCellQueries) = keptA;
        Global<void*>(addr::SC4QueryTool_FillCellQueries + 4) = keptB;
    }
    f.Add(today % 10);

    // The routes made again (kind 1 only has a line, after 0x004CAC50).
    if (kind == 1 && !s.buildings.empty()) {
        SetOccupant(tool, s.buildings[0].occupant);
        f.Add(This<uint8_t>(addr::SC4QueryTool_ShowRoutes, tool));
        HashLine(f, tool);
    }
    f.Add(Send(tool, message_id::kTrafficDone));
    HashLine(f, tool);

    // The view changed: OnMouseMove at fixed mouse positions.
    {
        SlotPatch cursor(Cdecl<void*>(addr::SC4_GetWinManager), 0x84, reinterpret_cast<void*>(&CursorAt));
        f.Add(uint8_t{cursor.Ok()});
        void* const view = At<void*>(tool, base::kView);
        SlotPatch clear(view, 0xA4, reinterpret_cast<void*>(&ViewClear));
        const int32_t points[][2] = {{512, 384}, {200, 200}, {800, 600}, {-1000, -1000}};
        for (const auto& p : points) {
            g_cursor[0] = p[0];
            g_cursor[1] = p[1];
            f.Add(Send(tool, message_id::kViewChanged));
            HashTool(f, tool);
        }
    }
    SetOccupant(tool, nullptr);
    At<uint8_t>(tool, field::kFlag94) = keptFlag94;
    At<uint8_t>(tool, field::kFlag78) = 1;
    HideTip(tool);
    char more[120];
    std::snprintf(more, sizeof more, "kind %u: %d tips, %d view texts; day %u", kind, tips, g_viewTexts, today);
    return Part(kind == 1 ? "messages1" : "messages0", f, more);
}

// --- 2: ui.tools.query7b's functions -------------------------------------------------------

uint32_t DriveSecond(char* tool, const Sample& s, int32_t sizeX, int32_t sizeZ)
{
    Fnv f;
    g_events = Fnv();
    int32_t pictures = 0;
    for (const Building& b : s.every) {
        SetOccupant(tool, b.occupant);
        Text title, text;
        uint32_t picture = 0xA5A5A5A5, meter = 0xA5A5A5A5;
        float share = 0.25f;
        This<void>(addr::SC4QueryTool_GetBuildingOccupantTipInfo, tool, title.Get(), text.Get(), &picture, &meter,
                   &share);
        f.Text(title.Read());
        f.Text(text.Read());
        f.Add(picture);
        f.Add(meter);
        f.Add(share);
        pictures += picture != 0xA5A5A5A5;
        Text lot;
        Cdecl<void>(addr::SC4QueryTool_AppendLotTexts, b.occupant, lot.Get());
        f.Text(lot.Read());
    }
    SetOccupant(tool, nullptr);
    std::vector<std::pair<int32_t, int32_t>> places = {{sizeX / 2, sizeZ / 2}, {0, 0}};
    for (const Building& b : s.buildings)
        places.push_back({b.x, b.z});
    for (const Thing& t : s.pieces)
        places.push_back({t.x, t.z});
    for (int32_t z = 3; z < sizeZ; z += 7)
        for (int32_t x = 3; x < sizeX; x += 7)
            places.push_back({x, z});
    std::size_t longest = 0;
    for (const auto& p : places) {
        SetPlace(tool, p.first, p.second);
        Text title, text;
        This<void>(addr::SC4QueryTool_GetCellTipInfo, tool, title.Get(), text.Get());
        f.Text(title.Read());
        f.Text(text.Read());
        if (text.Read().size() > longest)
            longest = text.Read().size();
    }
    char more[120];
    std::snprintf(more, sizeof more, "%d buildings (%d with a picture), %d places, longest place text %d",
                  static_cast<int32_t>(s.every.size()), pictures, static_cast<int32_t>(places.size()),
                  static_cast<int32_t>(longest));
    return Part("second", f, more);
}

// --- 4: OnMouseDownL on the tool of kind 1 -------------------------------------------------

uint32_t DriveClicks(char* tool, const Sample& s)
{
    Fnv f;
    g_events = Fnv();
    void* const view = At<void*>(tool, base::kView);
    SlotPatch clear(view, 0xA4, reinterpret_cast<void*>(&ViewClear));
    SlotPatch pick(view, 0x90, reinterpret_cast<void*>(&ViewPick));
    f.Add(uint8_t{clear.Ok() && pick.Ok()});
    std::vector<void*> asked = {nullptr};
    for (std::size_t i = 0; i < s.buildings.size() && i < 4; ++i)
        asked.push_back(s.buildings[i].occupant);
    for (const Thing& t : s.pieces)
        asked.push_back(t.occupant);
    int32_t clicks = 0;
    const auto click = [&](uint32_t modifiers) {
        f.Add(Virtual<uint8_t>(tool, 0x40, int32_t{-1000}, int32_t{-1000}, modifiers));
        HashTool(f, tool);
        HashLine(f, tool);
        f.Add(Virtual<uint8_t>(tool, 0x48, int32_t{-1000}, int32_t{-1000}, uint32_t{0}));
        f.Add(At<uint8_t>(tool, field::kPressed));
        ++clicks;
    };
    for (void* const occupant : asked) {
        SetOccupant(tool, occupant);
        click(0);
        click(4);
    }
    // Control: the ground point the stand-in gives, at a few cells.
    std::vector<std::pair<int32_t, int32_t>> cells = {{0, 0}, {33, 47}};
    for (std::size_t i = 0; i < s.buildings.size() && i < 3; ++i)
        cells.push_back({s.buildings[i].x, s.buildings[i].z});
    if (!s.buildings.empty())
        SetOccupant(tool, s.buildings[0].occupant);
    for (const auto& c : cells) {
        g_point[0] = static_cast<float>(c.first) * 16.0f + 8.0f;
        g_point[1] = kPlaceHeight;
        g_point[2] = static_cast<float>(c.second) * 16.0f + 8.0f;
        click(2);
    }
    SetOccupant(tool, nullptr);
    HideTip(tool);
    char more[80];
    std::snprintf(more, sizeof more, "%d clicks", clicks);
    return Part("clicks", f, more);
}

// --- 8: DescribePick and the kind 0 tool's clicks -------------------------------------------

// The screen points: a coarse grid over a 1024 by 768 view.
std::vector<std::pair<int32_t, int32_t>> Grid()
{
    std::vector<std::pair<int32_t, int32_t>> points;
    for (int32_t z = 120; z <= 660; z += 180)
        for (int32_t x = 112; x <= 912; x += 200)
            points.push_back({x, z});
    return points;
}

struct LogFolder {
    wchar_t folder[MAX_PATH] = {};
    wchar_t file[MAX_PATH] = {};
    bool ok = false;
    LogFolder()
    {
        wchar_t temp[MAX_PATH];
        const DWORD length = GetTempPathW(MAX_PATH, temp);
        if (!length || length >= MAX_PATH - 40)
            return;
        std::swprintf(folder, MAX_PATH, L"%lsopensc4-q7-%lu", temp, static_cast<unsigned long>(GetCurrentProcessId()));
        CreateDirectoryW(folder, nullptr);
        std::swprintf(file, MAX_PATH, L"%ls\\Query.txt", folder);
        ok = true;
    }
    ~LogFolder()
    {
        if (ok)
            RemoveDirectoryW(folder);
    }
    LogFolder(const LogFolder&) = delete;
    LogFolder& operator=(const LogFolder&) = delete;
    // The file's bytes, then the file deleted.
    std::string Take()
    {
        std::string bytes;
        const HANDLE handle = CreateFileW(file, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                          OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle == INVALID_HANDLE_VALUE)
            return bytes;
        char buffer[4096];
        DWORD read = 0;
        while (ReadFile(handle, buffer, sizeof buffer, &read, nullptr) && read)
            bytes.append(buffer, read);
        CloseHandle(handle);
        DeleteFileW(file);
        return bytes;
    }
};

uint32_t DriveDescribe(char* tool)
{
    Fnv f;
    g_events = Fnv();
    g_modal = Fnv();
    g_played = 0;
    const std::vector<std::pair<int32_t, int32_t>> points = Grid();
    void* const manager = Cdecl<void*>(addr::SC4_GetWinManager);
    SlotPatch modal(manager, 0xA4, reinterpret_cast<void*>(&ModalStandIn));
    f.Add(uint8_t{modal.Ok()});
    if (!modal.Ok())
        return Part("describe", f, "no window manager");
    LogFolder log;
    void* const sink = Global<void*>(addr::SC4QueryTool_LogSink);
    int32_t files = 0;
    std::size_t bytes = 0;
    const auto describe = [&](int32_t x, int32_t z, uint32_t debug) {
        Text first, second, third;
        wchar_t kept[MAX_PATH] = {};
        bool moved = false;
        if (debug && log.ok) {
            const DWORD length = GetCurrentDirectoryW(MAX_PATH, kept);
            moved = length && length < MAX_PATH && SetCurrentDirectoryW(log.folder);
        }
        if (debug && !moved)
            debug = 0;
        Cdecl<void>(addr::SC4QueryTool_DescribePick, x, z, first.Get(), second.Get(), third.Get(), debug);
        if (moved) {
            SetCurrentDirectoryW(kept);
            const std::string written = log.Take();
            files += !written.empty();
            bytes += written.size();
            f.Text(written);
        }
        f.Add(debug);
        f.Text(first.Read());
        f.Text(second.Read());
        f.Text(third.Read());
    };
    g_answer = 1;
    for (const auto& p : points) {
        describe(p.first, p.second, 0);
        describe(p.first, p.second, 1);
        f.Add(Virtual<uint8_t>(tool, 0x40, p.first, p.second, uint32_t{0}));
        f.Add(At<uint8_t>(tool, field::kPressed));
        f.Text(TextOf(reinterpret_cast<void*>(addr::SC4QueryTool_DebugText)));
        f.Text(TextOf(reinterpret_cast<void*>(addr::SC4QueryTool_DebugTextExtra)));
        f.Text(TextOf(reinterpret_cast<void*>(addr::SC4QueryTool_DebugTextExtra2)));
        f.Add(Virtual<uint8_t>(tool, 0x48, p.first, p.second, uint32_t{0}));
    }
    g_answer = 0;
    for (std::size_t i = 0; i < points.size() && i < 5; ++i)
        describe(points[i].first, points[i].second, 0);
    g_answer = 1;
    f.Add(uint8_t{Global<void*>(addr::SC4QueryTool_LogSink) != nullptr});
    Global<void*>(addr::SC4QueryTool_LogSink) = sink;
    f.Add(g_played);
    f.Add(g_modal.h);
    HideTip(tool);
    char more[120];
    std::snprintf(more, sizeof more, "%d points, %d windows run, %d log files (%d bytes)",
                  static_cast<int32_t>(points.size()), g_played, files, static_cast<int32_t>(bytes));
    return Part("describe", f, more);
}

// --- 16: shift clicks ------------------------------------------------------------------------

uint32_t DriveShift(char* tool)
{
    Fnv f;
    int32_t clicks = 0;
    for (const auto& p : Grid()) {
        Virtual<uint8_t>(tool, 0x40, p.first, p.second, uint32_t{1});
        Virtual<uint8_t>(tool, 0x48, p.first, p.second, uint32_t{0});
        ++clicks;
    }
    f.Add(clicks);
    HideTip(tool);
    char more[40];
    std::snprintf(more, sizeof more, "%d clicks", clicks);
    log::Write("autotest: query7: shift %08X (%s)", f.h, more);
    return f.h;
}

void Drive(const Day& day)
{
    if (!OnRunDay(day, "OPENSC4_TEST_Q7") || !game::IsBuild641())
        return;
    char* const view = CityView();
    if (!view || !day.city || !Global<void*>(addr::SC4_City) || !Global<void*>(addr::SC4_LotManager)) {
        log::Write("autotest: query7: no city view");
        return;
    }
    const bool commit = EnvInt("OPENSC4_TEST_Q7_COMMIT", 0) != 0;
    const int parts = EnvInt("OPENSC4_TEST_Q7_PARTS", commit ? 15 : 7) & (commit ? 31 : 23);
    SetUpFakes();
    const int32_t sizeX = static_cast<int32_t>(day.city->CellCountX());
    const int32_t sizeZ = static_cast<int32_t>(day.city->CellCountZ());
    Sample sample;
    Collect(sample, sizeX, sizeZ);
    char counts[160];
    std::snprintf(counts, sizeof counts, "%d buildings, %d network types, %d others; parts %d",
                  static_cast<int32_t>(sample.buildings.size()), static_cast<int32_t>(sample.pieces.size()),
                  static_cast<int32_t>(sample.others.size()), parts);
    log::Write("autotest: query7: %s", counts);
    Fnv total;
    total.Text(counts);
    if (parts & 3) {
        char* const tool = MakeTool(view, kMakeFirstTool);
        total.Add(uint8_t{tool != nullptr});
        if (tool) {
            if (parts & 1)
                total.Add(DriveMessages(tool, sample, sizeX, sizeZ));
            if (parts & 2)
                total.Add(DriveSecond(tool, sample, sizeX, sizeZ));
            PutAway(view, tool, total);
        } else {
            log::Write("autotest: query7: no query tool of kind 0 from the view");
            Virtual<uint8_t>(view, 0x68, uint32_t{0});
        }
    }
    if (parts & (1 | 4 | 16)) {
        char* const tool = MakeTool(view, kMakeSecondTool);
        total.Add(uint8_t{tool != nullptr});
        if (tool) {
            if (parts & 1)
                total.Add(DriveMessages(tool, sample, sizeX, sizeZ));
            if (parts & 4)
                total.Add(DriveClicks(tool, sample));
            if (parts & 16)
                total.Add(DriveShift(tool));
            PutAway(view, tool, total);
        } else {
            log::Write("autotest: query7: no query tool of kind 1 from the view");
            Virtual<uint8_t>(view, 0x68, uint32_t{0});
        }
    }
    if (parts & 8) {
        char* const tool = MakeTool(view, kMakeFirstTool);
        total.Add(uint8_t{tool != nullptr});
        if (tool) {
            total.Add(DriveDescribe(tool));
            PutAway(view, tool, total);
        } else {
            log::Write("autotest: query7: no query tool of kind 0 from the view");
            Virtual<uint8_t>(view, 0x68, uint32_t{0});
        }
    }
    for (const Thing& t : sample.pieces)
        Virtual<uint32_t>(t.occupant, 0x8);
    for (const Thing& t : sample.others)
        Virtual<uint32_t>(t.occupant, 0x8);
    total.Add(g_window.references);
    std::snprintf(g_token, sizeof g_token, " query7 %08X", total.h);
    log::Write("autotest: query7: total %08X (stand-in references %d)", total.h, g_window.references);
}

const DayHook kQuery7{"query7", 67, &Drive};

void Fingerprint(cISC4City*, std::string& line)
{
    line += g_token;
}

const FingerprintPart kFingerprint{"query7", 28, &Fingerprint};

} // namespace
} // namespace opensc4::autotest
