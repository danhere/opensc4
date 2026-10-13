// The dispatch, sign removal and Game of Life tools' part of the autotest
// (src/core/autotest_hooks.h; tool_dispatch.cpp, tool_signpick.cpp,
// tool_cellpaint.cpp): drives the three tools and hashes what comes out, so
// that a run with ui.tools.dispatch, ui.tools.signpick and ui.tools.cellpaint
// on and one with them off can be compared. Only the game's addresses and
// vtable slots are called, never our own functions.
//
//   OPENSC4_TEST_TOOLS3=<day>    token "tools3", sub-hashes on the log lines
//     "autotest: tools3: ...":
//
//   1. dispatch: for the police (0) and the fire (1) type, the tool made as
//      the view's commands make it (constructor 0x004BA7D0 on 0x90 new bytes
//      filled with 0x5A, the view's slot 0x64 with 2): the fields after the
//      constructor and after Init, the cursor text, the manager's counts;
//      the filter's slot 3 over every pylon and the first trees; the key
//      slot; the handlers at a point off the ground; OnMouseMove over a grid
//      of screen points (where a dispatch is allowed), once more with the
//      modifier bit 1; one OnMouseDownL off the ground (the stopwatch is
//      reset after it); PickMarker, Dispatch with type 8 and with `really`
//      false, PutDown with nothing held, SetHighlighted with null and
//      UpdateCursorText by address; DoMessage of the part at +0x28 with the
//      dispatch messages, an unknown one, and the tool-changed message in
//      its four shapes; put away by the view.
//   2. signs: the sign removal tool (constructor 0x004D76F0, the view's slot
//      0x64 with 2): whether signs and labels were shown before and are
//      after Init (put back at the end), the fields, the filter over every
//      prop (how many are signs), the handlers off the ground and over the
//      grid, SignAt and SetSign by address (with a sign of the city when it
//      has one), put away.
//   3. life: the grid's functions on a scratch grid (Resize, Paint in both
//      modes, Step with counting functions: a glider on 7 x 6 for 30
//      generations, a block for 200 so the age stops at 0xBF, a grid one
//      cell wide, the stop byte, a 16 x 16 from Resize with zeroes, a random
//      fill, which like every random fill here is logged and kept out of
//      the token); then the tool as the cheat makes it (constructor 0x004BC310,
//      the view's slot 0x64 with 2): the fields, the random grid of Init,
//      the keys (C, R, an unknown one), a press, a drag and a release on the
//      ground, three generations by the message target, the handlers off
//      the ground, put away.
//
//   OPENSC4_TEST_TOOLS3_COMMIT=1 adds what changes the city (a tutorial city
//   may hold it back: OPENSC4_TEST_TUTORIAL_OFF=1):
//
//   4. send: with the fire type, OnMouseDownL at the first grid point where
//      a dispatch is allowed; the manager's records of the type after it;
//      the new pylon made the manager's active one by hand (as a pick-up
//      does) and moved with OnMouseMove to the next allowed point; PutDown;
//      then every dispatch called off (the manager's slot 9). The records
//      after each step. OPENSC4_TEST_TOOLS3_SEND=0 leaves this step out.
//   5. remove: with a sign in the city, the first one (by position) is kept
//      with SetSign and removed with OnMouseUpL. A city without signs keeps
//      what it has.
//
// Not reached: anything with a marker picked on the screen (the pick reads
// what the last frame drew), so the pick-up branch of OnMouseDownL, the
// vehicle selection and the double click; the occupant-removed message
// (the tool never asks for it and it reads +0x5C unchecked). Every pointer
// is left out of the hashes: only whether it is set.
#include "core/autotest_hooks.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/log.h"

#include <opensc4/addresses.h>

#include <GZServPtrs.h>
#include <cIGZWin.h>
#include <cISC4App.h>
#include <cISC4View3DWin.h>
#include <cRZAutoRefCount.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
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

char g_token[40];

namespace field { // the base class (tool_base.cpp)
constexpr std::size_t kInited = 0x04, kCount = 0x08, kId = 0x0C, kCursorId = 0x10, kCursor = 0x14;
constexpr std::size_t kWindow = 0x18, kView = 0x1C;
} // namespace field

namespace dispatch { // tool_dispatch.cpp
constexpr std::size_t kTarget = 0x28, kType = 0x2C, kAllowed = 0x30, kGrabX = 0x34, kGrabZ = 0x38;
constexpr std::size_t kClickWatch = 0x40, kIdleCursor = 0x58, kHighlighted = 0x5C, kText = 0x60, kCurrent = 0x74;
constexpr std::size_t kFilter = 0x78, kStartPosition = 0x7C, kStartState = 0x88, kSize = 0x90;
} // namespace dispatch

namespace signs { // tool_signpick.cpp
constexpr std::size_t kFilter = 0x28, kSign = 0x2C, kSize = 0x30;
} // namespace signs

namespace life { // tool_cellpaint.cpp
constexpr std::size_t kTarget = 0x28, kTerrain = 0x2C, kCell = 0x30, kPerCell = 0x34, kLastX = 0x38, kLastZ = 0x3C;
constexpr std::size_t kGrid = 0x40, kOverlay = 0x64, kLive = 0x68, kSize = 0x88;
} // namespace life

namespace grid { // tool_cellpaint.cpp
constexpr std::size_t kStopped = 0x00, kErase = 0x04, kWidth = 0x08, kHeight = 0x0C, kCells = 0x10, kBefore = 0x14;
constexpr std::size_t kOnLive = 0x18, kOnDead = 0x1C, kContext = 0x20, kSize = 0x24;
} // namespace grid

constexpr uint32_t kTypePylon = 0xA9B6D7DE, kTypeProp = 0xA823821E, kTypeFlora = 0x74758926;
constexpr uint32_t kIidProp = 0xE9793A65;
constexpr uint32_t kSignOnTerrain = 0xAB72FBB3, kSignOnNetwork = 0xCB79919B;
constexpr int32_t kOff = -1000; // a screen point that picks nothing

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

// New memory filled with 0x5A, so that what a constructor leaves alone shows.
char* NewBlock(std::size_t size)
{
    char* const block = static_cast<char*>(Cdecl<void*>(addr::SC4_operator_new, static_cast<uint32_t>(size)));
    if (block)
        std::memset(block, 0x5A, size);
    return block;
}

// The base fields of a tool: the plain ones, and of the pointers whether
// they are set.
void HashTool(Fnv& f, char* tool)
{
    f.Add(At<uint8_t>(tool, field::kInited));
    f.Add(At<uint32_t>(tool, field::kCount));
    f.Add(At<uint32_t>(tool, field::kId));
    f.Add(At<uint32_t>(tool, field::kCursorId));
    for (const std::size_t offset : {field::kCursor, field::kWindow, field::kView})
        f.Add(uint8_t{At<void*>(tool, offset) != nullptr});
}

// The handlers: (x, z, modifiers) in the view window's pixels.
uint8_t MouseDownL(char* tool, int32_t x, int32_t z, uint32_t modifiers = 0)
{
    return Virtual<uint8_t>(tool, 0x40, x, z, modifiers);
}

uint8_t MouseUpL(char* tool, int32_t x, int32_t z)
{
    return Virtual<uint8_t>(tool, 0x48, x, z, uint32_t{0});
}

uint8_t MouseMove(char* tool, int32_t x, int32_t z, uint32_t modifiers = 0)
{
    return Virtual<uint8_t>(tool, 0x50, x, z, modifiers);
}

uint8_t KeyDown(char* tool, int32_t key)
{
    return Virtual<uint8_t>(tool, 0x38, key, uint32_t{0});
}

// A tool given to the view as its makers do: held, the view's slot 0x64.
// The reference stays ours until the caller lets go of it.
bool GiveToView(char* view, char* tool, uint32_t operation)
{
    Virtual<uint32_t>(tool, 0x4);
    Virtual<uint8_t>(view, 0x64, static_cast<void*>(tool), operation);
    return Virtual<void*>(view, 0x60) == tool;
}

// The screen points the tests try, row by row.
struct Point {
    int32_t x, z;
};

template <typename Fn>
void ForGrid(Fn&& fn)
{
    for (int32_t z = 60; z <= 700; z += 40)
        for (int32_t x = 40; x <= 980; x += 60)
            fn(Point{x, z});
}

// Every occupant of a type (the occupant manager's list by type, as
// flora_autotest.cpp walks it).
template <typename Fn>
void ForOccupants(void* city, uint32_t type, Fn&& fn)
{
    void* const occupants = Virtual<void*>(city, 0x88);
    if (!occupants)
        return;
    void** const sentinel = static_cast<void**>(Virtual<void*>(ListNodeAllocator(), 0xC, 0xCu));
    sentinel[0] = sentinel;
    sentinel[1] = sentinel;
    void* head = sentinel;
    Virtual<uint8_t>(occupants, 0x9C, &head, 0u, 0u, type, -1);
    for (void** node = static_cast<void**>(sentinel[0]); node != sentinel; node = static_cast<void**>(node[0]))
        fn(node[2]);
    Virtual<uint8_t>(occupants, 0xC0, &head);
    reinterpret_cast<void(__thiscall*)(void*)>(addr::SC4List_Destroy2)(&head);
}

// A message of the given type and two numbers to a tool's message target at
// +0x28 (its slot 3); the answer.
uint8_t Send(char* tool, uint32_t type, void* first = nullptr, void* second = nullptr)
{
    void* const memory = Cdecl<void*>(addr::RZ_operator_new, std::size_t{0x2C});
    void* const message = memory ? This<void*>(addr::cGZMessage_cGZMessage, memory) : nullptr;
    if (!message)
        return 0xEE;
    Virtual<uint32_t>(message, 0x4);
    Virtual<void>(message, 0x14, type);
    Virtual<void>(message, 0x2C, first);
    Virtual<void>(message, 0x40, second);
    void* const target = tool + 0x28;
    const uint8_t answer = Virtual<uint8_t>(target, 0xC, message);
    Virtual<uint32_t>(message, 0x8);
    return answer;
}

// --- 1 and 4. The dispatch tool ------------------------------------------------------------

// The cursor text at +0x60.
std::string Text(char* tool)
{
    const char* const begin = At<const char*>(tool, dispatch::kText + 4);
    const char* const end = At<const char*>(tool, dispatch::kText + 8);
    return begin && end >= begin ? std::string(begin, end) : std::string("?");
}

void HashDispatch(Fnv& f, char* tool)
{
    HashTool(f, tool);
    f.Add(At<uint32_t>(tool, dispatch::kType));
    f.Add(At<uint8_t>(tool, dispatch::kAllowed));
    f.Add(At<int32_t>(tool, dispatch::kGrabX));
    f.Add(At<int32_t>(tool, dispatch::kGrabZ));
    f.Add(This<uint8_t>(addr::GZStopwatch_IsRunning, tool + dispatch::kClickWatch));
    f.Add(At<uint32_t>(tool, dispatch::kIdleCursor));
    f.Add(uint8_t{At<void*>(tool, dispatch::kHighlighted) != nullptr});
    f.Add(At<uint8_t>(tool, dispatch::kCurrent));
    f.Add(uint8_t{At<void*>(tool, dispatch::kFilter) != nullptr});
    for (std::size_t i = 0; i < 4; ++i)
        f.Add(At<uint32_t>(tool, dispatch::kStartPosition + i * 4)); // the place and the state
    const std::string text = Text(tool);
    f.Bytes(text.data(), text.size());
}

// The manager's records of a type: how many, and of each the ID, the plain
// numbers, the place, and which of its three objects are set.
struct Records {
    uint32_t count = 0;
    uint32_t free = 0;
    uint32_t hash = 0;
    void* pylon = nullptr; // of the last record
    uint32_t place[3] = {};
    uint32_t numbers[2] = {};
};

Records ReadRecords(void* manager, uint32_t type)
{
    Records r;
    Fnv f;
    r.count = Virtual<uint32_t>(manager, 0x2C, type); // GetDispatchCount
    r.free = Virtual<uint32_t>(manager, 0x54, type);  // GetAvailablePylonCount
    f.Add(r.count);
    f.Add(r.free);
    uint32_t ids[32] = {};
    uint32_t room = 32;
    const uint32_t got = Virtual<uint32_t>(manager, 0x30, type, &ids[0], &room); // GetDispatchIDArray
    f.Add(got);
    for (uint32_t i = 0; i < got && i < 32; ++i) {
        uint32_t info[11] = {};
        const uint8_t found = Virtual<uint8_t>(manager, 0x28, ids[i], &info[0]); // GetDispatchInfo
        f.Add(found);
        for (std::size_t k = 0; k < 8; ++k)
            f.Add(k == 4 ? (info[k] & 0xFF) : info[k]);
        for (std::size_t k = 8; k < 11; ++k)
            f.Add(uint8_t{info[k] != 0});
        r.pylon = reinterpret_cast<void*>(static_cast<std::uintptr_t>(info[10]));
        std::memcpy(r.place, &info[5], sizeof r.place);
        std::memcpy(r.numbers, &info[2], sizeof r.numbers);
    }
    r.hash = f.h;
    return r;
}

// A pylon: its occupant's place and its three states.
void HashPylon(Fnv& f, void* pylon)
{
    uint32_t place[3] = {};
    Virtual<void>(Virtual<void*>(pylon, 0xC), 0x20, &place[0]);
    f.Add(place);
    f.Add(Virtual<uint32_t>(pylon, 0x1C)); // the UI state
    f.Add(Virtual<uint32_t>(pylon, 0x30)); // the dispatch state
    f.Add(Virtual<uint8_t>(pylon, 0x40));  // dragged
}

char* MakeDispatchTool(char* view, uint32_t type, Fnv& f)
{
    char* const tool = NewBlock(dispatch::kSize);
    if (!tool || !This<void*>(addr::SC4DispatchTool_Construct, tool, type))
        return nullptr;
    HashDispatch(f, tool);
    f.Add(uint8_t{At<std::uintptr_t>(tool, 0) == addr::SC4DispatchTool_Vtable});
    f.Add(uint8_t{At<std::uintptr_t>(tool, dispatch::kTarget) == addr::SC4DispatchTool_TargetVtable});
    if (!GiveToView(view, tool, 2)) {
        log::Write("autotest: tools3: the view did not take the dispatch tool");
        return nullptr;
    }
    HashDispatch(f, tool);
    return tool;
}

void ResetWatch(char* tool)
{
    This<void>(addr::SC4Stopwatch_Reset, tool + dispatch::kClickWatch);
}

// The grid points where the tool says a dispatch is allowed.
struct Allowed {
    Point at[2] = {};
    int32_t count = 0;
};

uint32_t DriveDispatch(char* view, void* city, uint32_t type, Allowed& allowed)
{
    Fnv f;
    void* const manager = Virtual<void*>(city, 0x9C);
    f.Add(uint8_t{Virtual<void*>(view, 0x60) != nullptr});
    const Records before = ReadRecords(manager, type);
    f.Add(before.hash);
    char* const tool = MakeDispatchTool(view, type, f);
    if (!tool)
        return 0;
    const uint32_t made = f.h;
    const std::string text = Text(tool);

    // The filter: every pylon of the city, and the first trees.
    void* const filter = At<void*>(tool, dispatch::kFilter);
    f.Add(uint8_t{filter && At<std::uintptr_t>(filter, 0) == addr::SC4DispatchTool_FilterVtable});
    int32_t pylons = 0, taken = 0, trees = 0;
    if (filter) {
        ForOccupants(city, kTypePylon, [&](void* occupant) {
            ++pylons;
            const uint8_t answer = Virtual<uint8_t>(filter, 0xC, occupant);
            taken += answer;
            f.Add(answer);
        });
        ForOccupants(city, kTypeFlora, [&](void* occupant) {
            if (trees++ < 20)
                f.Add(Virtual<uint8_t>(filter, 0xC, occupant));
        });
    }
    f.Add(pylons);

    const auto step = [&](uint8_t answer) {
        f.Add(answer);
        HashDispatch(f, tool);
    };
    step(KeyDown(tool, 9));
    step(KeyDown(tool, 0x41));
    step(MouseMove(tool, kOff, kOff));
    step(MouseDownL(tool, kOff, kOff));
    ResetWatch(tool);
    step(MouseMove(tool, kOff, kOff));
    const uint32_t off = f.h;

    // The hover over the grid.
    int32_t tried = 0, ground = 0;
    ForGrid([&](Point p) {
        ++tried;
        f.Add(MouseMove(tool, p.x, p.z));
        const uint8_t ok = At<uint8_t>(tool, dispatch::kAllowed);
        f.Add(ok);
        if (ok && allowed.count++ < 2)
            allowed.at[allowed.count - 1] = p;
        float position[3] = {0.0f, 0.0f, 0.0f};
        ground += Virtual<uint8_t>(view, 0x90, p.x, p.z, &position[0], uint32_t{0});
    });
    f.Add(ground);
    if (allowed.count) {
        f.Add(MouseMove(tool, allowed.at[0].x, allowed.at[0].z, 2)); // the modifier bit 1: not allowed
        f.Add(At<uint8_t>(tool, dispatch::kAllowed));
    }
    HashDispatch(f, tool);
    const uint32_t hover = f.h;

    // By address.
    void* picked = reinterpret_cast<void*>(std::uintptr_t{0x5A5A5A5A});
    f.Add(This<uint8_t>(addr::SC4DispatchTool_PickMarker, tool, kOff, kOff, &picked));
    f.Add(uint8_t{picked != nullptr});
    float place[3] = {64.0f, 0.0f, 64.0f};
    f.Add(This<uint8_t>(addr::SC4DispatchTool_Dispatch, tool, uint32_t{8}, &place[0], uint32_t{1}));
    f.Add(This<uint8_t>(addr::SC4DispatchTool_Dispatch, tool, type, &place[0], uint32_t{0}));
    f.Add(This<uint8_t>(addr::SC4DispatchTool_PutDown, tool));
    This<void>(addr::SC4DispatchTool_SetHighlighted, tool, static_cast<void*>(nullptr));
    This<void>(addr::SC4DispatchTool_UpdateCursorText, tool);
    HashDispatch(f, tool);
    const uint32_t direct = f.h;

    // The messages.
    for (const uint32_t message : {0x894671F6u, 0xE94671EBu, 0xE94671ECu, 0xE94671EDu, 0x12345678u, 0xE94671EEu})
        step(Send(tool, message));
    step(Send(tool, 0x09C8B8EA, tool, tool));                 // from itself to itself: nothing
    step(Send(tool, 0x09C8B8EA, nullptr, nullptr));           // not about this tool
    step(Send(tool, 0x09C8B8EA, tool, nullptr));              // it goes: not current, the text cleared
    step(Send(tool, 0x894671F6));                             // not current: no text
    step(Send(tool, 0x09C8B8EA, nullptr, tool));              // it comes: current, the text again
    const uint32_t messages = f.h;

    f.Add(Virtual<uint8_t>(view, 0x68, uint32_t{0}));
    const bool away = Virtual<void*>(view, 0x60) == nullptr;
    f.Add(uint8_t{away});
    HashDispatch(f, tool);
    const Records after = ReadRecords(manager, type);
    f.Add(after.hash);
    const uint32_t last = Virtual<uint32_t>(tool, 0x8);
    f.Add(last);
    log::Write("autotest: tools3: dispatch type %u %08X (made %08X, text '%s', records %u, free units %u; %d pylons, "
               "%d taken by the filter; off the ground %08X; hover %08X: %d points, %d on the ground, %d allowed, "
               "first at %d,%d; by address %08X; messages %08X; away %d, records after %08X vs %08X, last release %u)",
               type, f.h, made, text.c_str(), before.count, before.free, pylons, taken, off, hover, tried, ground,
               allowed.count, allowed.at[0].x, allowed.at[0].z, direct, messages, away, after.hash, before.hash, last);
    return f.h;
}

uint32_t DriveSend(char* view, void* city, uint32_t type, const Allowed& allowed)
{
    Fnv f;
    void* const manager = Virtual<void*>(city, 0x9C);
    f.Add(allowed.count);
    if (!allowed.count) {
        log::Write("autotest: tools3: send: no point where a dispatch is allowed, nothing sent");
        return f.h;
    }
    char* const tool = MakeDispatchTool(view, type, f);
    if (!tool)
        return 0;
    const auto records = [&](const char* name) {
        const Records r = ReadRecords(manager, type);
        f.Add(r.hash);
        if (r.pylon)
            HashPylon(f, r.pylon);
        log::Write("autotest: tools3: send %s: records %u, free units %u, hash %08X, last: numbers %08X %08X at %08X "
                   "%08X %08X, pylon %d, active pylon %d, text '%s'",
                   name, r.count, r.free, r.hash, r.numbers[0], r.numbers[1], r.place[0], r.place[1], r.place[2],
                   r.pylon != nullptr, Virtual<void*>(manager, 0x48) != nullptr, Text(tool).c_str());
        return r;
    };
    records("before");

    // The click on the ground.
    const uint8_t down = MouseDownL(tool, allowed.at[0].x, allowed.at[0].z);
    ResetWatch(tool);
    f.Add(down);
    HashDispatch(f, tool);
    const Records sent = records("after the click");

    // The new pylon held, as a pick-up leaves it, and moved.
    uint8_t put = 0xEE;
    if (sent.pylon && sent.count) {
        void* const pylon = sent.pylon;
        Virtual<void>(manager, 0x4C, pylon); // SetActivePylon
        Virtual<void>(Virtual<void*>(pylon, 0xC), 0x20, static_cast<void*>(tool + dispatch::kStartPosition));
        At<uint32_t>(tool, dispatch::kStartState) = Virtual<uint32_t>(pylon, 0x30);
        At<int32_t>(tool, dispatch::kGrabX) = 0;
        At<int32_t>(tool, dispatch::kGrabZ) = 0;
        const Point to = allowed.at[allowed.count > 1 ? 1 : 0];
        f.Add(MouseMove(tool, to.x, to.z));
        HashDispatch(f, tool);
        HashPylon(f, pylon);
        records("held and moved");
        put = This<uint8_t>(addr::SC4DispatchTool_PutDown, tool);
        f.Add(put);
        records("put down");
    }
    f.Add(Virtual<uint8_t>(manager, 0x24, uint32_t{0})); // every dispatch called off
    const Records end = records("called off");

    f.Add(Virtual<uint8_t>(view, 0x68, uint32_t{0}));
    const bool away = Virtual<void*>(view, 0x60) == nullptr;
    f.Add(uint8_t{away});
    HashDispatch(f, tool);
    const uint32_t last = Virtual<uint32_t>(tool, 0x8);
    f.Add(last);
    log::Write("autotest: tools3: send %08X (click at %d,%d answered %u, records then %u; put down answered %u; "
               "records at the end %u; away %d, last release %u)",
               f.h, allowed.at[0].x, allowed.at[0].z, down, sent.count, put, end.count, away, last);
    return f.h;
}

// --- 2 and 5. The sign removal tool --------------------------------------------------------

void HashSigns(Fnv& f, char* tool)
{
    HashTool(f, tool);
    f.Add(uint8_t{At<void*>(tool, signs::kFilter) != nullptr});
    f.Add(uint8_t{At<void*>(tool, signs::kSign) != nullptr});
}

// A sign of the city: the prop interface (not referenced) and its place.
struct Sign {
    std::array<uint32_t, 3> place{};
    uint32_t kind = 0;
    void* prop = nullptr;
    bool operator<(const Sign& other) const
    {
        return place != other.place ? place < other.place : kind < other.kind;
    }
};

std::vector<Sign> CitySigns(void* city, int32_t& props)
{
    std::vector<Sign> found;
    ForOccupants(city, kTypeProp, [&](void* occupant) {
        ++props;
        void* prop = nullptr;
        if (!Virtual<uint8_t>(occupant, 0x0, kIidProp, &prop) || !prop)
            return;
        const uint32_t kind = Virtual<uint32_t>(prop, 0x1C);
        if (kind == kSignOnTerrain || kind == kSignOnNetwork) {
            Sign sign;
            sign.kind = kind;
            sign.prop = prop;
            Virtual<void>(occupant, 0x20, &sign.place[0]);
            found.push_back(sign);
        }
        Virtual<uint32_t>(prop, 0x8);
    });
    std::sort(found.begin(), found.end());
    return found;
}

uint32_t DriveSigns(char* view, void* city, bool commit)
{
    Fnv f;
    void* const developer = Global<void*>(addr::SC4_Service43DB8);
    const uint8_t shown = developer ? Virtual<uint8_t>(developer, 0x28) : 9; // AreSignsAndLabelsVisible
    f.Add(uint8_t{Virtual<void*>(view, 0x60) != nullptr});
    char* const tool = NewBlock(signs::kSize);
    if (!developer || !tool || !This<void*>(addr::SC4SignRemoveTool_Construct, tool))
        return 0;
    HashSigns(f, tool);
    f.Add(uint8_t{At<std::uintptr_t>(tool, 0) == addr::SC4SignRemoveTool_Vtable});
    if (!GiveToView(view, tool, 2)) {
        log::Write("autotest: tools3: the view did not take the sign tool");
        return 0;
    }
    HashSigns(f, tool);
    const uint8_t shownInTool = Virtual<uint8_t>(developer, 0x28);
    f.Add(shownInTool);

    // The filter over every prop.
    void* const filter = At<void*>(tool, signs::kFilter);
    f.Add(uint8_t{filter && At<std::uintptr_t>(filter, 0) == addr::SC4SignRemoveTool_FilterVtable});
    int32_t props = 0, taken = 0, others = 0;
    if (filter) {
        ForOccupants(city, kTypeProp, [&](void* occupant) { taken += Virtual<uint8_t>(filter, 0xC, occupant); });
        ForOccupants(city, kTypeFlora, [&](void* occupant) {
            if (others++ < 20)
                f.Add(Virtual<uint8_t>(filter, 0xC, occupant));
        });
    }
    std::vector<Sign> found = CitySigns(city, props);
    f.Add(props);
    f.Add(taken);
    f.Add(static_cast<int32_t>(found.size()));

    // Off the ground, and over the grid (a sign under a point is kept).
    const auto step = [&](uint8_t answer) {
        f.Add(answer);
        HashSigns(f, tool);
    };
    step(MouseMove(tool, kOff, kOff));
    step(MouseUpL(tool, kOff, kOff));
    step(MouseDownL(tool, kOff, kOff));
    f.Add(uint8_t{This<void*>(addr::SC4SignRemoveTool_SignAt, tool, kOff, kOff) != nullptr});
    int32_t hovered = 0;
    ForGrid([&](Point p) {
        f.Add(MouseMove(tool, p.x, p.z));
        const uint8_t over = At<void*>(tool, signs::kSign) != nullptr;
        hovered += over;
        f.Add(over);
    });
    This<void>(addr::SC4SignRemoveTool_SetSign, tool, static_cast<void*>(nullptr));
    HashSigns(f, tool);
    const uint32_t handlers = f.h;

    // With a sign of the city: kept twice, let go; with the commit removed.
    uint8_t removed = 0;
    int32_t left = static_cast<int32_t>(found.size());
    if (!found.empty()) {
        void* const sign = found.front().prop;
        f.Add(found.front().kind);
        f.Add(found.front().place);
        This<void>(addr::SC4SignRemoveTool_SetSign, tool, sign);
        HashSigns(f, tool);
        This<void>(addr::SC4SignRemoveTool_SetSign, tool, sign);
        HashSigns(f, tool);
        if (commit) {
            removed = MouseUpL(tool, kOff, kOff);
            f.Add(removed);
            HashSigns(f, tool);
            int32_t again = 0;
            left = static_cast<int32_t>(CitySigns(city, again).size());
            f.Add(left);
        } else {
            This<void>(addr::SC4SignRemoveTool_SetSign, tool, static_cast<void*>(nullptr));
            HashSigns(f, tool);
        }
    }

    f.Add(Virtual<uint8_t>(view, 0x68, uint32_t{0}));
    const bool away = Virtual<void*>(view, 0x60) == nullptr;
    f.Add(uint8_t{away});
    HashSigns(f, tool);
    const uint32_t last = Virtual<uint32_t>(tool, 0x8);
    f.Add(last);
    // Signs and labels as they were.
    if (shown != shownInTool)
        Virtual<void>(developer, 0x20, uint32_t{shown});
    const uint8_t shownAfter = Virtual<uint8_t>(developer, 0x28);
    log::Write("autotest: tools3: signs %08X (signs and labels shown %u, in the tool %u, put back to %u; %d props, %d "
               "taken by the filter, %d signs; handlers %08X, %d grid points over a sign; commit %d: removed %u, "
               "signs left %d; away %d, last release %u)",
               f.h, shown, shownInTool, shownAfter, props, taken, static_cast<int32_t>(found.size()), handlers, hovered,
               commit, removed, left, away, last);
    return f.h;
}

// --- 3. The Game of Life tool --------------------------------------------------------------

struct Calls {
    uint32_t live = 0, dead = 0, sum = 0;
};

void __cdecl CountLive(uint32_t x, uint32_t z, void* context)
{
    Calls* const calls = static_cast<Calls*>(context);
    ++calls->live;
    calls->sum = calls->sum * 31 + x * 7 + z;
}

void __cdecl CountDead(uint32_t x, uint32_t z, void* context)
{
    Calls* const calls = static_cast<Calls*>(context);
    ++calls->dead;
    calls->sum = calls->sum * 31 + x * 5 + z + 1;
}

// A grid's sizes, mode and both arrays.
void HashGrid(Fnv& f, char* cells)
{
    f.Add(At<uint8_t>(cells, grid::kStopped));
    f.Add(At<uint32_t>(cells, grid::kErase));
    const uint32_t width = At<uint32_t>(cells, grid::kWidth), height = At<uint32_t>(cells, grid::kHeight);
    f.Add(width);
    f.Add(height);
    for (const std::size_t array : {grid::kCells, grid::kBefore}) {
        const uint8_t* const bytes = At<const uint8_t*>(cells, array);
        f.Add(uint8_t{bytes != nullptr});
        if (bytes && width <= 4096 && height <= 4096)
            f.Bytes(bytes, static_cast<std::size_t>(width) * height);
    }
}

uint32_t LiveCells(char* cells)
{
    const uint8_t* const bytes = At<const uint8_t*>(cells, grid::kCells);
    const uint32_t count = At<uint32_t>(cells, grid::kWidth) * At<uint32_t>(cells, grid::kHeight);
    uint32_t live = 0;
    for (uint32_t i = 0; bytes && i < count; ++i)
        live += bytes[i] != 0;
    return live;
}

uint8_t Paint(char* cells, uint32_t x, uint32_t z)
{
    return This<uint8_t>(addr::SC4LifeGrid_Paint, cells, x, z);
}

void Resize(char* cells, uint32_t width, uint32_t height, uint32_t random)
{
    This<void>(addr::SC4LifeGrid_Resize, cells, width, height, random);
}

void Step(char* cells)
{
    This<void>(addr::SC4LifeGrid_Step, cells);
}

uint32_t DriveGrid(uint32_t& randomHash, uint32_t& randomLive)
{
    Fnv f;
    alignas(4) char cells[grid::kSize] = {};
    Calls calls;

    // A glider on 7 x 6, wrapped, with both functions counting.
    Resize(cells, 7, 6, 0);
    HashGrid(f, cells);
    for (const Point p : {Point{1, 0}, Point{2, 1}, Point{0, 2}, Point{1, 2}, Point{2, 2}})
        f.Add(Paint(cells, static_cast<uint32_t>(p.x), static_cast<uint32_t>(p.z)));
    f.Add(Paint(cells, 1, 0)); // already live
    HashGrid(f, cells);
    At<void*>(cells, grid::kOnLive) = reinterpret_cast<void*>(&CountLive);
    At<void*>(cells, grid::kOnDead) = reinterpret_cast<void*>(&CountDead);
    At<void*>(cells, grid::kContext) = &calls;
    for (int32_t i = 0; i < 30; ++i) {
        Step(cells);
        HashGrid(f, cells);
    }
    f.Add(calls);
    const uint32_t glider = f.h;
    const uint32_t gliderLive = LiveCells(cells);

    // Erasing; a mode that is neither; the stop byte.
    At<uint32_t>(cells, grid::kErase) = 1;
    for (uint32_t z = 0; z < 6; ++z)
        for (uint32_t x = 0; x < 7; x += 2)
            f.Add(Paint(cells, x, z));
    HashGrid(f, cells);
    At<uint32_t>(cells, grid::kErase) = 2;
    f.Add(Paint(cells, 3, 3));
    At<uint32_t>(cells, grid::kErase) = 0;
    f.Add(Paint(cells, 3, 3));
    At<uint8_t>(cells, grid::kStopped) = 1;
    Step(cells);
    HashGrid(f, cells);
    At<uint8_t>(cells, grid::kStopped) = 0;

    // A block on 6 x 6 without the functions: the age stops at 0xBF.
    At<void*>(cells, grid::kOnLive) = nullptr;
    At<void*>(cells, grid::kOnDead) = nullptr;
    Resize(cells, 6, 6, 0);
    for (const Point p : {Point{2, 2}, Point{3, 2}, Point{2, 3}, Point{3, 3}})
        Paint(cells, static_cast<uint32_t>(p.x), static_cast<uint32_t>(p.z));
    for (int32_t i = 0; i < 200; ++i) {
        Step(cells);
        if (i % 10 == 9)
            HashGrid(f, cells);
    }
    const uint8_t oldest = At<const uint8_t*>(cells, grid::kCells)[2 * 6 + 2];
    f.Add(oldest);

    // One cell wide, then one cell high: a cell is its own neighbour.
    Resize(cells, 1, 5, 0);
    Paint(cells, 0, 1);
    Paint(cells, 0, 2);
    for (int32_t i = 0; i < 4; ++i) {
        Step(cells);
        HashGrid(f, cells);
    }
    Resize(cells, 5, 1, 0);
    Paint(cells, 1, 0);
    Paint(cells, 3, 0);
    for (int32_t i = 0; i < 4; ++i) {
        Step(cells);
        HashGrid(f, cells);
    }
    Resize(cells, 1, 1, 0);
    Paint(cells, 0, 0);
    Step(cells);
    HashGrid(f, cells);

    // No size: 16 x 16.
    Resize(cells, 0, 0, 0);
    HashGrid(f, cells);
    const uint32_t plain = f.h;

    // A random fill (a generator seeded by the clock, which the fixed seeds
    // replace by the call site's).
    Resize(cells, 24, 10, 1);
    Fnv r;
    HashGrid(r, cells);
    randomHash = r.h;
    randomLive = LiveCells(cells);
    for (int32_t i = 0; i < 5; ++i)
        Step(cells);
    HashGrid(r, cells);

    Cdecl<void>(addr::SC4_operator_delete, At<void*>(cells, grid::kCells));
    Cdecl<void>(addr::SC4_operator_delete, At<void*>(cells, grid::kBefore));
    log::Write("autotest: tools3: grid %08X (glider %08X: %u live after 30, calls %u live %u dead; block's age after "
               "200: %02X; plain %08X; random 24 x 10: %08X, %u live, after 5 more %08X)",
               f.h, glider, gliderLive, calls.live, calls.dead, oldest, plain, randomHash, randomLive, r.h);
    return f.h;
}

void HashLife(Fnv& f, char* tool)
{
    HashTool(f, tool);
    f.Add(uint8_t{At<void*>(tool, life::kTerrain) != nullptr});
    f.Add(At<uint32_t>(tool, life::kCell));
    f.Add(At<uint32_t>(tool, life::kPerCell));
    f.Add(At<int32_t>(tool, life::kLastX));
    f.Add(At<int32_t>(tool, life::kLastZ));
    char* const cells = tool + life::kGrid;
    HashGrid(f, cells);
    f.Add(uint8_t{At<void*>(cells, grid::kOnLive) != nullptr});
    f.Add(uint8_t{At<void*>(cells, grid::kOnDead) != nullptr});
    f.Add(uint8_t{At<void*>(cells, grid::kContext) != nullptr});
    void* const overlay = At<void*>(tool, life::kOverlay);
    f.Add(uint8_t{overlay != nullptr});
    // The live cells the tool listed, and what the overlay has.
    const uint32_t* const begin = At<const uint32_t*>(tool, life::kLive);
    const uint32_t* const end = At<const uint32_t*>(tool, life::kLive + 4);
    f.Add(static_cast<uint32_t>(end - begin));
    if (begin)
        f.Bytes(begin, static_cast<std::size_t>(end - begin) * 4);
    if (overlay) {
        const uint32_t* const first = At<const uint32_t*>(overlay, 0x10);
        const uint32_t* const last = At<const uint32_t*>(overlay, 0x14);
        f.Add(static_cast<uint32_t>(last - first));
        if (first)
            f.Bytes(first, static_cast<std::size_t>(last - first) * 4);
    }
}

uint32_t DriveLife(char* view, uint32_t& randomHash)
{
    Fnv f;
    f.Add(uint8_t{Virtual<void*>(view, 0x60) != nullptr});
    char* const tool = NewBlock(life::kSize);
    if (!tool || !This<void*>(addr::SC4LifeTool_Construct, tool))
        return 0;
    HashLife(f, tool);
    f.Add(uint8_t{At<std::uintptr_t>(tool, 0) == addr::SC4LifeTool_Vtable});
    f.Add(uint8_t{At<std::uintptr_t>(tool, life::kTarget) == addr::SC4LifeTool_TargetVtable});
    const uint32_t constructed = f.h;
    if (!GiveToView(view, tool, 2)) {
        log::Write("autotest: tools3: the view did not take the life tool");
        return 0;
    }
    char* const cells = tool + life::kGrid;
    // Init filled the grid at random: hashed apart, and emptied at once so
    // that everything after is the same whatever the seed.
    Fnv r;
    HashLife(r, tool);
    randomHash = r.h;
    const uint32_t width = At<uint32_t>(cells, grid::kWidth), height = At<uint32_t>(cells, grid::kHeight);
    const uint32_t randomLive = LiveCells(cells);
    const uint8_t callbackIsGames = At<std::uintptr_t>(cells, grid::kOnLive) == addr::SC4LifeTool_CellLives;
    f.Add(callbackIsGames);
    f.Add(KeyDown(tool, 0x43)); // C
    HashLife(f, tool);
    f.Add(KeyDown(tool, 0x41)); // not a key of the tool
    f.Add(KeyDown(tool, 0x52)); // R: random again
    Fnv r2;
    HashLife(r2, tool);
    const uint32_t randomAgain = r2.h;
    f.Add(KeyDown(tool, 0x2E)); // Delete
    f.Add(KeyDown(tool, 0x08)); // Backspace
    HashLife(f, tool);
    const uint32_t keys = f.h;

    // Off the ground; then a press, a drag and a release on the ground.
    f.Add(MouseMove(tool, kOff, kOff));
    f.Add(MouseDownL(tool, kOff, kOff));
    f.Add(MouseUpL(tool, kOff, kOff));
    HashLife(f, tool);
    std::vector<Point> ground;
    ForGrid([&](Point p) {
        float position[3] = {0.0f, 0.0f, 0.0f};
        if (Virtual<uint8_t>(view, 0x90, p.x, p.z, &position[0], uint32_t{0}) && position[0] >= 0.0f &&
            position[2] >= 0.0f && position[0] < static_cast<float>(width) * 16.0f &&
            position[2] < static_cast<float>(height) * 16.0f)
            ground.push_back(p);
    });
    f.Add(static_cast<int32_t>(ground.size()));
    int32_t painted = 0;
    if (ground.size() >= 4) {
        const Point a = ground[ground.size() / 2], b = ground[ground.size() / 2 + 1], c = ground[ground.size() / 2 + 2];
        f.Add(MouseMove(tool, b.x, b.z)); // no press yet: nothing
        HashLife(f, tool);
        f.Add(MouseDownL(tool, a.x, a.z)); // a dead cell: the drag paints
        HashLife(f, tool);
        f.Add(MouseMove(tool, b.x, b.z));
        f.Add(MouseMove(tool, b.x, b.z)); // the same cell
        f.Add(MouseMove(tool, c.x, c.z));
        HashLife(f, tool);
        f.Add(MouseUpL(tool, c.x, c.z));
        f.Add(MouseMove(tool, a.x, a.z)); // released: nothing
        HashLife(f, tool);
        painted = static_cast<int32_t>(LiveCells(cells));
        f.Add(MouseDownL(tool, a.x, a.z)); // a live cell: the drag erases
        f.Add(MouseMove(tool, b.x, b.z));
        f.Add(MouseUpL(tool, b.x, b.z));
        HashLife(f, tool);
    }
    const uint32_t drag = f.h;

    // A glider near the corner by hand, and three generations by message.
    At<uint32_t>(cells, grid::kErase) = 0;
    for (const Point p : {Point{1, 0}, Point{2, 1}, Point{0, 2}, Point{1, 2}, Point{2, 2}})
        Paint(cells, static_cast<uint32_t>(p.x), static_cast<uint32_t>(p.z));
    for (int32_t i = 0; i < 3; ++i) {
        f.Add(Send(tool, 0x86AD10EE));
        HashLife(f, tool);
    }
    f.Add(Send(tool, 0x12345678)); // any message is a generation
    HashLife(f, tool);
    const uint32_t generations = f.h;
    const uint32_t liveAtEnd = LiveCells(cells);

    // No generations between here and the view taking the tool off.
    At<uint8_t>(cells, grid::kStopped) = 1;
    f.Add(Virtual<uint8_t>(view, 0x68, uint32_t{0}));
    const bool away = Virtual<void*>(view, 0x60) == nullptr;
    f.Add(uint8_t{away});
    HashLife(f, tool);
    const uint32_t last = Virtual<uint32_t>(tool, 0x8);
    f.Add(last);
    log::Write("autotest: tools3: life %08X (constructed %08X; grid %u x %u, random after Init %08X with %u live, "
               "after R %08X, the grid's live-cell function is the game's address %u; keys %08X; %d grid points on "
               "the ground, %d cells after the drag, drag %08X; generations %08X, %u live; away %d, last release %u)",
               f.h, constructed, width, height, randomHash, randomLive, randomAgain, callbackIsGames, keys,
               static_cast<int32_t>(ground.size()), painted, drag, generations, liveAtEnd, away, last);
    return f.h;
}

void Drive(const Day& day)
{
    if (!OnRunDay(day, "OPENSC4_TEST_TOOLS3") || !game::IsBuild641())
        return;
    char* const view = CityView();
    void* const city = Global<void*>(addr::SC4_City);
    if (!view || !city || !Global<void*>(addr::SC4_Terrain) || !Virtual<void*>(city, 0x9C)) {
        log::Write("autotest: tools3: no city view");
        return;
    }
    const bool commit = EnvInt("OPENSC4_TEST_TOOLS3_COMMIT", 0) != 0;
    const bool send = commit && EnvInt("OPENSC4_TEST_TOOLS3_SEND", 1) != 0;
    const int parts = EnvInt("OPENSC4_TEST_TOOLS3_PARTS", 7); // 1 dispatch, 2 signs, 4 life

    Fnv all;
    Allowed police, fire;
    if (parts & 1) {
        all.Add(DriveDispatch(view, city, 0, police));
        all.Add(DriveDispatch(view, city, 1, fire));
        if (send)
            all.Add(DriveSend(view, city, 1, fire));
    }
    if (parts & 2)
        all.Add(DriveSigns(view, city, commit));
    if (parts & 4) {
        // The random fills are logged and not in the token.
        uint32_t randomGrid = 0, randomLive = 0, randomTool = 0;
        all.Add(DriveGrid(randomGrid, randomLive));
        all.Add(DriveLife(view, randomTool));
    }
    std::snprintf(g_token, sizeof g_token, " tools3 %08X", all.h);
}

const DayHook kTools3{"tools3", 57, &Drive};

void Fingerprint(cISC4City*, std::string& line)
{
    line += g_token;
}

const FingerprintPart kFingerprint{"tools3", 17, &Fingerprint};

} // namespace
} // namespace opensc4::autotest
