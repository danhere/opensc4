// The disaster tools' part of the autotest (src/core/autotest_hooks.h;
// tool_effect.cpp, tool_disaster.cpp, tool_torch.cpp): drives the common
// disaster tool (with the effect trigger tool under it) and the torch and
// hashes what comes out, so that a run with ui.tools.disaster_effect,
// ui.tools.disaster and ui.tools.disaster_fire on and one with them off can
// be compared. Only the game's addresses and vtable slots are called, never
// our own functions.
//
//   OPENSC4_TEST_TOOLS6=<day>    token "tools6", sub-hashes on the log lines
//     "autotest: tools6: ...". Everything here leaves the city as it was:
//
//   1. common: every exemplar of group 0x29B6C670 (sorted by ID; its three
//      properties are on the log). For each one the view's StartDisaster
//      (0x007E9DB0) takes: the tool the view made (the fields after the
//      constructor, the set-up calls and Init); the handlers at a point off
//      the ground (a move, a press that begins nothing, a release that
//      answers false, the pointer leaving); moves over a grid of screen
//      points (the inactive effect follows the pointer); the pointer
//      leaving; the view puts the tool away (Shutdown); the last reference.
//      No press on the ground: a press starts the tool's effects, and some
//      of them are the disaster.
//   2. torch: the tool the view makes for 0xA9DE8784: the fields; a move off
//      the ground, moves over the grid without the button (the inactive
//      torch follows the pointer), the pointer leaving, a release (the torch
//      is out already), MoveTorch, DropTorch and SetBurning by address with
//      no torch, the frame message and another while it does not burn; the
//      view puts the tool away.
//   OPENSC4_TEST_TOOLS6_PARTS: 1 common, 2 torch.
//
//   OPENSC4_TEST_TOOLS6_COMMIT=1 adds what changes the city (use with
//   OPENSC4_TEST_TUTORIAL_OFF=1). The option that has the camera go to a
//   disaster (0x00B43CD8 + 0xF08) is turned off first.
//
//   3. torch: a move to a point on the ground, a press there (a forced fire
//      at the point's cell) and the release at once, so that no frame
//      message reaches a burning torch; point after point from the middle of
//      the grid until the disaster layer counts a disaster under way (at
//      most 60 points). With OPENSC4_TEST_TOOLS6_FRAME=1 one frame message is
//      delivered by hand before the release, with the cell kept as a move
//      would keep it (a fire by chance; on the log only).
//   4. effect disaster: the first exemplar whose tool has a control effect
//      and no manager of its type (the tool's +0xA4 is 0): a move, a press and a release on the ground
//      (slot 30 with 1: the effect manager's part makes an instance), then
//      the view puts the tool away. The day after, a new tool of the same
//      exemplar is given the instance and its slot 30 is called with 0 (the
//      instance ends); the token changes with that. Ended in the hook that
//      started it, the game exits without a word within the day, the
//      original too.
//   OPENSC4_TEST_TOOLS6_LEGS picks the commit's legs: 1 torch, 2 effect, 4
//   typed (default 7).
//   5. typed disaster: the first exemplar with a type (property 0x4A08C701)
//      that is not the volcano's or the earthquake's: a press and a release
//      at a ground point whose truncated metres are cells of the city (the
//      tool hands the manager's slot 6 metres); skipped with a log line when
//      there is no such exemplar or point.
//
// Kept out of the hashes: every pointer (only whether it is set), +0x2A of
// the common tool before a press (the constructor leaves it), whether an
// effect runs, the torch's stopwatch and generator; they are on the
// "varies" log lines where they are of interest.
#include "core/autotest_hooks.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/log.h"

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
uint32_t g_hash = 0;          // the token's value
int32_t g_ranOn = -1;         // the day the test ran
void* g_instance = nullptr;   // the effect commit's instance, held for the day after
uint32_t g_instanceOf = 0;    // and its exemplar

namespace field { // the base class (tool_base.cpp)
constexpr std::size_t kInited = 0x04, kCount = 0x08, kId = 0x0C, kCursorId = 0x10, kCursor = 0x14;
constexpr std::size_t kWindow = 0x18, kView = 0x1C, kTarget = 0x28;
} // namespace field

namespace common { // tool_effect.cpp, tool_disaster.cpp
constexpr std::size_t kPressed = 0x28, kStarted = 0x29, kIdle = 0x2A, kPlace = 0x2C, kTransform = 0x38;
constexpr std::size_t kEffects = 0x70, kNameBegin = 0x78, kNameEnd = 0x7C, kNumber = 0x88, kFlags = 0x8C;
constexpr std::size_t kButton = 0x90, kControl = 0x94, kSingle = 0x98, kActive = 0x9C;
constexpr std::size_t kInactive = 0xA0, kHasType = 0xA4, kType = 0xA8, kManager = 0xAC, kInstance = 0xB0;
} // namespace common

namespace torch { // tool_torch.cpp
constexpr std::size_t kFires = 0x2C, kTorch = 0x30, kPlace = 0x34, kCellX = 0x40, kCellZ = 0x44, kBurning = 0x60;
} // namespace torch

namespace layer { // the disaster layer (docs/disasters.md)
constexpr std::size_t kUnderWay = 0x48, kLocations = 0xCC;
} // namespace layer

constexpr uint32_t kMessageFrame = 0x86AD10EE, kFrameCode = 0xC0;
constexpr uint32_t kFireTool = 0xA9DE8784;
constexpr uint32_t kExemplarType = 0x6534284A, kExemplarIid = 0xA52160F5, kDisasterGroup = 0x29B6C670;
constexpr uint32_t kPropertyName = 0x4A08C700, kPropertyType = 0x4A08C701, kPropertyNumber = 0x4A08C702;
constexpr uint32_t kVolcano = 0xC4C90997, kEarthquake = 0x42963812;
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
uint8_t MouseDownL(char* tool, int32_t x, int32_t z)
{
    return Virtual<uint8_t>(tool, 0x40, x, z, uint32_t{0});
}

uint8_t MouseUpL(char* tool, int32_t x, int32_t z)
{
    return Virtual<uint8_t>(tool, 0x48, x, z, uint32_t{0});
}

uint8_t MouseMove(char* tool, int32_t x, int32_t z)
{
    return Virtual<uint8_t>(tool, 0x50, x, z, uint32_t{0});
}

uint8_t MouseExit(char* tool)
{
    return Virtual<uint8_t>(tool, 0x58);
}

// A message of a type to the message target part of a tool (its slot 3).
void Message(char* tool, uint32_t type)
{
    Deliver(tool + field::kTarget - 0x4, type, nullptr, 0, 0);
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

// A grid point with ground under it: the view's pick (slot 0x90), as the
// tools ask.
struct Ground {
    Point at{};
    float point[3] = {};
};

// The points well inside the city, and all of them.
void GroundPoints(char* view, cISC4City* city, std::vector<Ground>* inside, std::vector<Ground>* all)
{
    const float sizeX = static_cast<float>(city->CellCountX()) * 16.0f;
    const float sizeZ = static_cast<float>(city->CellCountZ()) * 16.0f;
    constexpr float kMargin = 160.0f;
    ForGrid([&](Point p) {
        Ground g;
        g.at = p;
        if (!Virtual<uint8_t>(view, 0x90, p.x, p.z, &g.point[0], uint32_t{0}))
            return;
        if (g.point[0] >= 0.0f && g.point[2] >= 0.0f && g.point[0] < sizeX && g.point[2] < sizeZ)
            all->push_back(g);
        if (g.point[0] >= kMargin && g.point[2] >= kMargin && g.point[0] < sizeX - kMargin &&
            g.point[2] < sizeZ - kMargin)
            inside->push_back(g);
    });
}

// The disaster layer's object (the city's interface to it is at +8).
char* Layer(cISC4City* city)
{
    void* const part = city->GetDisasterLayer();
    return part ? static_cast<char*>(part) - 0x8 : nullptr;
}

int32_t UnderWay(char* disasters)
{
    return At<int32_t>(disasters, layer::kUnderWay);
}

int32_t Locations(char* disasters)
{
    const char* const begin = At<char*>(disasters, layer::kLocations);
    const char* const end = At<char*>(disasters, layer::kLocations + 4);
    return static_cast<int32_t>((end - begin) / 0x14);
}

// The tool the view makes for a disaster's ID (its StartDisaster), held for
// the caller; null when the view made none.
char* MakeTool(char* view, uint32_t id)
{
    Virtual<uint8_t>(view, 0x68, uint32_t{0});
    This<void>(addr::SC4UI_F7E9DB0, view, id);
    char* const tool = Virtual<char*>(view, 0x60);
    if (tool)
        Virtual<uint32_t>(tool, 0x4);
    return tool;
}

// The view puts its tool away; whether it has none after that.
bool PutAway(char* view)
{
    Virtual<uint8_t>(view, 0x68, uint32_t{0});
    return Virtual<void*>(view, 0x60) == nullptr;
}

// --- The exemplars -------------------------------------------------------------------------

// The resource manager's filter by type and group, made on the stack.
struct TypeGroupFilter {
    std::uintptr_t vtable;
    uint32_t references;
    uint32_t type;
    uint32_t group;
};

struct Disaster {
    uint32_t id = 0;
    uint8_t hasName = 0, hasNumber = 0, hasType = 0;
    uint32_t number = 0, type = 0;
};

std::vector<Disaster> Disasters()
{
    std::vector<Disaster> found;
    void* const resources = Global<void*>(addr::SC4_ResourceManager);
    if (!resources)
        return found;
    TypeGroupFilter filter;
    filter.vtable = addr::SC4KeyFilter_TypeGroupVtable;
    filter.references = 0;
    filter.type = kExemplarType;
    filter.group = kDisasterGroup;
    void* list = nullptr;
    if (!Virtual<uint32_t>(resources, 0x44, &list, &filter) || !list)
        return found;
    const uint32_t count = Virtual<uint32_t>(list, 0x24);
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t* const key = Virtual<const uint32_t*>(list, 0x28, i);
        void* exemplar = nullptr;
        if (!Virtual<uint8_t>(resources, 0xC, key, kExemplarIid, &exemplar, uint32_t{0}, uint32_t{0}) || !exemplar)
            continue;
        Disaster d;
        d.id = key[2];
        void* const holder = Virtual<void*>(exemplar, 0x1C);
        d.hasName = Virtual<uint8_t>(holder, 0xC, kPropertyName);
        d.hasNumber = Cdecl<uint8_t>(addr::SC4_GetPropertyUint32, holder, kPropertyNumber, &d.number);
        d.hasType = Cdecl<uint8_t>(addr::SC4_GetPropertyUint32, holder, kPropertyType, &d.type);
        Virtual<uint32_t>(exemplar, 0x8);
        if (std::none_of(found.begin(), found.end(), [&](const Disaster& other) { return other.id == d.id; }))
            found.push_back(d);
    }
    Virtual<uint32_t>(list, 0x8);
    std::sort(found.begin(), found.end(), [](const Disaster& a, const Disaster& b) { return a.id < b.id; });
    return found;
}

// --- The common disaster tool ----------------------------------------------------------------

std::string NameOf(char* tool)
{
    const char* const begin = At<const char*>(tool, common::kNameBegin);
    const char* const end = At<const char*>(tool, common::kNameEnd);
    return begin && end >= begin ? std::string(begin, end) : std::string();
}

// Every field but the pointers (whether they are set) and +0x2A.
void HashCommon(Fnv& f, char* tool)
{
    HashTool(f, tool);
    f.Add(At<uint8_t>(tool, common::kPressed));
    f.Add(At<uint8_t>(tool, common::kStarted));
    f.Bytes(tool + common::kPlace, 0xC);
    f.Bytes(tool + common::kTransform, 0x2);        // changes, flags
    f.Bytes(tool + common::kTransform + 0x4, 0x34); // rotation, position, scale
    const std::string name = NameOf(tool);
    f.Add(static_cast<uint32_t>(name.size()));
    f.Bytes(name.data(), name.size());
    f.Add(At<uint32_t>(tool, common::kNumber));
    f.Bytes(tool + common::kFlags, 0x4); // any, button trigger, trigger, the pick flag
    for (const std::size_t offset : {common::kEffects, common::kButton, common::kControl, common::kSingle,
                                     common::kActive, common::kInactive, common::kManager, common::kInstance})
        f.Add(uint8_t{At<void*>(tool, offset) != nullptr});
    f.Add(At<uint8_t>(tool, common::kHasType));
    f.Add(At<uint32_t>(tool, common::kType));
}

// Which of the five effects the tool has: bits button, control, single,
// active, inactive.
uint32_t EffectBits(char* tool)
{
    uint32_t bits = 0, bit = 1;
    for (const std::size_t offset :
         {common::kButton, common::kControl, common::kSingle, common::kActive, common::kInactive}) {
        if (At<void*>(tool, offset))
            bits |= bit;
        bit <<= 1;
    }
    return bits;
}

uint32_t DriveCommon(char* view, const Disaster& d)
{
    Fnv f;
    f.Add(d.id);
    char* const tool = MakeTool(view, d.id);
    if (!tool) {
        log::Write("autotest: tools6: common %08X: the view made no tool", d.id);
        return f.h;
    }
    const bool ours = At<std::uintptr_t>(tool, 0) == addr::SC4DisasterTool_Vtable;
    f.Add(uint8_t{ours});
    if (!ours) {
        log::Write("autotest: tools6: common %08X: the view's tool is of another class", d.id);
        PutAway(view);
        Virtual<uint32_t>(tool, 0x8);
        return f.h;
    }
    HashCommon(f, tool);
    const uint32_t made = f.h;
    const uint32_t bits = EffectBits(tool);
    const uint8_t idleAsMade = At<uint8_t>(tool, common::kIdle);
    const uint8_t flags[4] = {At<uint8_t>(tool, common::kFlags), At<uint8_t>(tool, common::kFlags + 1),
                              At<uint8_t>(tool, common::kFlags + 2), At<uint8_t>(tool, common::kFlags + 3)};

    const auto step = [&](uint8_t answer) {
        f.Add(answer);
        HashCommon(f, tool);
    };
    // Off the ground.
    step(MouseMove(tool, kOff, kOff));
    const uint8_t offDown = MouseDownL(tool, kOff, kOff);
    step(offDown);
    const uint8_t offUp = MouseUpL(tool, kOff, kOff);
    step(offUp);
    const uint8_t offExit = MouseExit(tool);
    step(offExit);
    const uint32_t off = f.h;

    // Over the grid.
    int32_t onGround = 0;
    ForGrid([&](Point p) {
        const uint8_t before = At<uint8_t>(tool, common::kTransform);
        step(MouseMove(tool, p.x, p.z));
        onGround += At<uint8_t>(tool, common::kTransform) != before;
    });
    step(MouseExit(tool));
    step(MouseMove(tool, kOff, kOff));
    const uint32_t moves = f.h;

    // The view puts the tool away.
    const bool away = PutAway(view);
    f.Add(uint8_t{away});
    HashCommon(f, tool);
    const uint32_t lastRelease = Virtual<uint32_t>(tool, 0x8);
    f.Add(lastRelease);
    log::Write("autotest: tools6: common %08X: %08X (made %08X: name '%s', number %08X, any %u, button trigger %u, "
               "trigger %u, pick flag %u, effects %02X, has type %u, type %08X; off the ground %08X: press %u, release "
               "%u, exit %u; moves %08X, %d on the ground; away %d, last release %u)",
               d.id, f.h, made, NameOf(tool).c_str(), At<uint32_t>(tool, common::kNumber), flags[0], flags[1],
               flags[2], flags[3], bits, At<uint8_t>(tool, common::kHasType), At<uint32_t>(tool, common::kType), off,
               offDown, offUp, offExit, moves, onGround, away, lastRelease);
    log::Write("autotest: tools6: varies: common %08X: +0x2A as made %02X", d.id, idleAsMade);
    return f.h;
}

// --- The torch ---------------------------------------------------------------------------------

// Every field but the pointers (whether they are set), the generator and
// the stopwatch.
void HashTorch(Fnv& f, char* tool)
{
    HashTool(f, tool);
    f.Add(uint8_t{At<void*>(tool, torch::kFires) != nullptr});
    f.Add(uint8_t{At<void*>(tool, torch::kTorch) != nullptr});
    f.Bytes(tool + torch::kPlace, 0xC);
    f.Add(At<int32_t>(tool, torch::kCellX));
    f.Add(At<int32_t>(tool, torch::kCellZ));
    f.Add(At<uint8_t>(tool, torch::kBurning));
}

// The view's torch, or null with a log line.
char* MakeTorch(char* view)
{
    char* const tool = MakeTool(view, kFireTool);
    if (!tool) {
        log::Write("autotest: tools6: torch: the view made no tool");
        return nullptr;
    }
    if (At<std::uintptr_t>(tool, 0) != addr::SC4TorchTool_Vtable ||
        At<std::uintptr_t>(tool, field::kTarget) != addr::SC4TorchTool_TargetVtable) {
        log::Write("autotest: tools6: torch: the view's tool is of another class");
        PutAway(view);
        Virtual<uint32_t>(tool, 0x8);
        return nullptr;
    }
    return tool;
}

uint32_t DriveTorch(char* view)
{
    Fnv f;
    char* const tool = MakeTorch(view);
    if (!tool)
        return f.h;
    HashTorch(f, tool);
    const uint32_t made = f.h;
    const uint8_t fires = At<void*>(tool, torch::kFires) != nullptr;
    const auto step = [&](uint8_t answer) {
        f.Add(answer);
        HashTorch(f, tool);
    };
    step(MouseMove(tool, kOff, kOff));
    int32_t onGround = 0;
    ForGrid([&](Point p) {
        step(MouseMove(tool, p.x, p.z));
        onGround += At<void*>(tool, torch::kTorch) != nullptr;
    });
    const uint32_t moves = f.h;
    const uint8_t exit = MouseExit(tool);
    step(exit);
    const uint8_t up = MouseUpL(tool, kOff, kOff);
    step(up);
    // By address, with no torch: nothing but DropTorch's sound.
    This<void>(addr::SC4TorchTool_MoveTorch, tool);
    This<void>(addr::SC4TorchTool_SetBurning, tool, uint32_t{1});
    This<void>(addr::SC4TorchTool_SetBurning, tool, uint32_t{0});
    This<void>(addr::SC4TorchTool_DropTorch, tool);
    HashTorch(f, tool);
    // A torch again, put out by SetBurning (as it is) and dropped by address.
    step(MouseMove(tool, 520, 380));
    This<void>(addr::SC4TorchTool_MoveTorch, tool);
    This<void>(addr::SC4TorchTool_SetBurning, tool, uint32_t{0});
    HashTorch(f, tool);
    This<void>(addr::SC4TorchTool_DropTorch, tool);
    HashTorch(f, tool);
    // Messages while it does not burn.
    Message(tool, kMessageFrame);
    Message(tool, 0x12345678);
    HashTorch(f, tool);
    const uint32_t idle = f.h;

    const bool away = PutAway(view);
    f.Add(uint8_t{away});
    HashTorch(f, tool);
    const uint32_t lastRelease = Virtual<uint32_t>(tool, 0x8);
    f.Add(lastRelease);
    log::Write("autotest: tools6: torch %08X (made %08X, fire part %u; moves %08X, a torch after %d of them; exit %u, "
               "release %u; idle %08X; away %d, last release %u)",
               f.h, made, fires, moves, onGround, exit, up, idle, away, lastRelease);
    return f.h;
}

// 3. A press on the ground and the release at once.
uint32_t CommitTorch(char* view, cISC4City* city, const std::vector<Ground>& ground, bool frame)
{
    Fnv f;
    char* const disasters = Layer(city);
    char* const tool = MakeTorch(view);
    if (!tool || !disasters || ground.empty()) {
        log::Write("autotest: tools6: torch commit: nothing to do it with");
        if (tool) {
            PutAway(view);
            Virtual<uint32_t>(tool, 0x8);
        }
        return f.h;
    }
    const int32_t before = UnderWay(disasters), placesBefore = Locations(disasters);
    f.Add(before);
    int32_t tries = 0, cellX = -1, cellZ = -1;
    Point at{};
    const std::size_t count = ground.size();
    for (std::size_t i = 0; i < count && tries < 60 && UnderWay(disasters) == before; ++i) {
        // From the middle of the list outwards.
        const std::size_t index = (count / 2 + (i % 2 ? count - (i + 1) / 2 : i / 2)) % count;
        const Ground& g = ground[index];
        ++tries;
        at = g.at;
        f.Add(MouseMove(tool, g.at.x, g.at.z));
        HashTorch(f, tool);
        Virtual<void>(Global<void*>(addr::SC4_City), 0x17C, g.point[0], g.point[2], &cellX, &cellZ);
        f.Add(MouseDownL(tool, g.at.x, g.at.z));
        HashTorch(f, tool);
        if (frame) {
            At<int32_t>(tool, torch::kCellX) = cellX;
            At<int32_t>(tool, torch::kCellZ) = cellZ;
            Message(tool, kMessageFrame);
            log::Write("autotest: tools6: varies: torch commit: a frame message at cell %d,%d: %d under way", cellX,
                       cellZ, UnderWay(disasters));
        }
        f.Add(MouseUpL(tool, g.at.x, g.at.z));
        if (!frame)
            HashTorch(f, tool);
    }
    const int32_t after = UnderWay(disasters), placesAfter = Locations(disasters);
    f.Add(tries);
    f.Add(cellX);
    f.Add(cellZ);
    if (!frame) {
        f.Add(after);
        f.Add(placesAfter - placesBefore);
    }
    const bool away = PutAway(view);
    f.Add(uint8_t{away});
    if (void* const frames = Cdecl<void*>(addr::SC4_GetService86AD10EC))
        Virtual<uint8_t>(frames, 0x18, static_cast<void*>(tool + field::kTarget), kFrameCode);
    const uint32_t lastRelease = Virtual<uint32_t>(tool, 0x8);
    f.Add(lastRelease);
    log::Write("autotest: tools6: torch commit %08X (%d presses, the last at %d,%d, cell %d,%d; disasters under way %d "
               "to %d, locations %d to %d; away %d, last release %u)",
               f.h, tries, at.x, at.z, cellX, cellZ, before, after, placesBefore, placesAfter, away, lastRelease);
    return f.h;
}

// 4. The effect disaster through the common tool's slot 30.
uint32_t CommitEffect(char* view, cISC4City* city, const std::vector<Disaster>& all,
                      const std::vector<Ground>& ground)
{
    Fnv f;
    char* const disasters = Layer(city);
    if (!disasters || ground.empty()) {
        log::Write("autotest: tools6: effect commit: no layer or no ground");
        return f.h;
    }
    const Ground& g = ground[ground.size() / 2];
    for (const Disaster& d : all) {
        if (!d.hasName && !d.hasNumber)
            continue;
        char* const tool = MakeTool(view, d.id);
        if (!tool)
            continue;
        const bool fits = At<std::uintptr_t>(tool, 0) == addr::SC4DisasterTool_Vtable &&
                          At<void*>(tool, common::kControl) && !At<uint8_t>(tool, common::kHasType);
        if (!fits) {
            PutAway(view);
            Virtual<uint32_t>(tool, 0x8);
            continue;
        }
        f.Add(d.id);
        const int32_t before = UnderWay(disasters), placesBefore = Locations(disasters);
        f.Add(MouseMove(tool, g.at.x, g.at.z));
        HashCommon(f, tool);
        const uint8_t down = MouseDownL(tool, g.at.x, g.at.z);
        f.Add(down);
        HashCommon(f, tool);
        const uint8_t idle = At<uint8_t>(tool, common::kIdle);
        const uint8_t up = MouseUpL(tool, g.at.x, g.at.z);
        f.Add(up);
        HashCommon(f, tool);
        const int32_t started = UnderWay(disasters), placesStarted = Locations(disasters);
        const uint8_t instance = At<void*>(tool, common::kInstance) != nullptr;
        f.Add(started - before);
        f.Add(placesStarted - placesBefore);
        // The instance is ended the day after (EndEffect): ended in this
        // hook, with the effect started a moment ago, the game exits
        // without a word within the day (the original too: Making Money
        // d0090). The test's own reference keeps the instance until then.
        if (void* const kept = At<void*>(tool, common::kInstance)) {
            Virtual<uint32_t>(kept, 0x4);
            g_instance = kept;
            g_instanceOf = d.id;
        }
        const bool away = PutAway(view);
        f.Add(uint8_t{away});
        HashCommon(f, tool);
        const uint32_t lastRelease = Virtual<uint32_t>(tool, 0x8);
        log::Write("autotest: tools6: effect commit %08X (exemplar %08X at %d,%d: press %u, release %u, instance %u; "
                   "disasters under way %d, %d after the release; locations %d, %d; away %d)",
                   f.h, d.id, g.at.x, g.at.z, down, up, instance, before, started, placesBefore, placesStarted, away);
        log::Write("autotest: tools6: varies: effect commit: +0x2A after the press %u, last release %u", idle,
                   lastRelease);
        return f.h;
    }
    log::Write("autotest: tools6: effect commit: no exemplar's tool has a control effect and no type");
    return f.h;
}

// 4, the day after: the instance of the effect commit is ended through a
// new tool of the same exemplar: the tool is given the instance (as its
// slot 30 with 1 keeps it) and its slot 30 is called with 0.
uint32_t EndEffect(char* view, cISC4City* city)
{
    Fnv f;
    char* const disasters = Layer(city);
    void* const instance = g_instance;
    g_instance = nullptr;
    char* const tool = disasters ? MakeTool(view, g_instanceOf) : nullptr;
    if (!tool || At<std::uintptr_t>(tool, 0) != addr::SC4DisasterTool_Vtable || At<void*>(tool, common::kInstance)) {
        log::Write("autotest: tools6: effect end: no tool to end the instance with");
        if (tool) {
            PutAway(view);
            Virtual<uint32_t>(tool, 0x8);
        }
        return f.h;
    }
    const int32_t before = UnderWay(disasters), placesBefore = Locations(disasters);
    At<void*>(tool, common::kInstance) = instance; // the test's reference goes to the tool
    Virtual<void>(tool, 0x78, uint32_t{0});
    HashCommon(f, tool);
    const int32_t ended = UnderWay(disasters), placesEnded = Locations(disasters);
    f.Add(ended - before);
    f.Add(placesEnded - placesBefore);
    // The instance outlives the tool: the effect may still know its address.
    Virtual<uint32_t>(instance, 0x4);
    const bool away = PutAway(view);
    f.Add(uint8_t{away});
    HashCommon(f, tool);
    Virtual<uint32_t>(tool, 0x8);
    log::Write("autotest: tools6: effect end %08X (exemplar %08X: slot 30 with 0: disasters under way %d to %d, "
               "locations %d to %d; away %d)",
               f.h, g_instanceOf, before, ended, placesBefore, placesEnded, away);
    return f.h;
}

// 5. A typed disaster through the common tool's release.
uint32_t CommitTyped(char* view, cISC4City* city, const std::vector<Disaster>& all, const std::vector<Ground>& ground)
{
    Fnv f;
    char* const disasters = Layer(city);
    const Disaster* chosen = nullptr;
    int32_t typed = 0;
    for (const Disaster& d : all) {
        if (!d.hasType || d.type == 0)
            continue;
        ++typed;
        if (!chosen && d.type != kVolcano && d.type != kEarthquake)
            chosen = &d;
    }
    if (!disasters || !chosen) {
        log::Write("autotest: tools6: typed commit: skipped, %d exemplars with a type and none usable", typed);
        return f.h;
    }
    // A point whose truncated metres are cells of the city.
    const Ground* where = nullptr;
    const auto cellsX = static_cast<float>(city->CellCountX()), cellsZ = static_cast<float>(city->CellCountZ());
    for (const Ground& g : ground)
        if (!where && g.point[0] < cellsX && g.point[2] < cellsZ)
            where = &g;
    if (!where) {
        log::Write("autotest: tools6: typed commit: skipped, exemplar %08X of type %08X: no ground point whose metres "
                   "are cells of the city",
                   chosen->id, chosen->type);
        return f.h;
    }
    char* const tool = MakeTool(view, chosen->id);
    if (!tool || At<std::uintptr_t>(tool, 0) != addr::SC4DisasterTool_Vtable) {
        log::Write("autotest: tools6: typed commit: the view made no common tool for %08X", chosen->id);
        if (tool) {
            PutAway(view);
            Virtual<uint32_t>(tool, 0x8);
        }
        return f.h;
    }
    f.Add(chosen->id);
    const int32_t before = UnderWay(disasters), placesBefore = Locations(disasters);
    f.Add(MouseMove(tool, where->at.x, where->at.z));
    const uint8_t down = MouseDownL(tool, where->at.x, where->at.z);
    f.Add(down);
    HashCommon(f, tool);
    // No slot 30 at the release: only the manager's start.
    At<uint8_t>(tool, common::kIdle) = 0;
    const uint8_t up = MouseUpL(tool, where->at.x, where->at.z);
    f.Add(up);
    HashCommon(f, tool);
    const int32_t after = UnderWay(disasters), placesAfter = Locations(disasters);
    f.Add(after - before);
    const bool away = PutAway(view);
    f.Add(uint8_t{away});
    Virtual<uint32_t>(tool, 0x8);
    log::Write("autotest: tools6: typed commit %08X (exemplar %08X, type %08X, at %d,%d = %.1f, %.1f: press %u, "
               "release %u; disasters under way %d to %d, locations %d to %d; away %d)",
               f.h, chosen->id, chosen->type, where->at.x, where->at.z, static_cast<double>(where->point[0]),
               static_cast<double>(where->point[2]), down, up, before, after, placesBefore, placesAfter, away);
    return f.h;
}

void Drive(const Day& day)
{
    if (g_instance && g_ranOn >= 0 && day.day == g_ranOn + 1) {
        if (char* const view = CityView()) {
            Fnv later;
            later.Add(g_hash);
            later.Add(EndEffect(view, day.city));
            g_hash = later.h;
            std::snprintf(g_token, sizeof g_token, " tools6 %08X", g_hash);
        }
    }
    if (!OnRunDay(day, "OPENSC4_TEST_TOOLS6") || !game::IsBuild641())
        return;
    char* const view = CityView();
    if (!view || !day.city || !Global<void*>(addr::SC4_City) || !Global<void*>(addr::SC4_Service43D1C)) {
        log::Write("autotest: tools6: no city view");
        return;
    }
    const bool commit = EnvInt("OPENSC4_TEST_TOOLS6_COMMIT", 0) != 0;
    const bool frame = EnvInt("OPENSC4_TEST_TOOLS6_FRAME", 0) != 0;
    const int parts = EnvInt("OPENSC4_TEST_TOOLS6_PARTS", 3); // 1 common, 2 torch

    std::vector<Ground> ground, everywhere;
    GroundPoints(view, day.city, &ground, &everywhere);
    const std::vector<Disaster> all = Disasters();
    Fnv total;
    total.Add(static_cast<int32_t>(ground.size()));
    total.Add(static_cast<int32_t>(all.size()));
    for (const Disaster& d : all) {
        total.Add(d.id);
        total.Add(d.hasName);
        total.Add(d.hasNumber);
        total.Add(d.number);
        total.Add(d.hasType);
        total.Add(d.type);
        log::Write("autotest: tools6: exemplar %08X: name (0x4A08C700) %u, number (0x4A08C702) %u %08X, type "
                   "(0x4A08C701) %u %08X",
                   d.id, d.hasName, d.hasNumber, d.number, d.hasType, d.type);
    }
    log::Write("autotest: tools6: %d exemplars in group 0x29B6C670, %d ground points of the grid inside the city, %d "
               "in all",
               static_cast<int32_t>(all.size()), static_cast<int32_t>(ground.size()),
               static_cast<int32_t>(everywhere.size()));
    const bool hadTool = Virtual<void*>(view, 0x60) != nullptr;
    total.Add(uint8_t{hadTool});
    if (commit) {
        if (char* const options = Global<char*>(addr::SC4_Options43CD8))
            At<uint8_t>(options, 0xF08) = 0;
    }
    if (parts & 1) {
        for (const Disaster& d : all)
            if (d.hasName || d.hasNumber)
                total.Add(DriveCommon(view, d));
    }
    if (parts & 2)
        total.Add(DriveTorch(view));
    if (commit) {
        const int legs = EnvInt("OPENSC4_TEST_TOOLS6_LEGS", 7); // 1 torch, 2 effect, 4 typed
        if ((parts & 2) && (legs & 1))
            total.Add(CommitTorch(view, day.city, ground, frame));
        if ((parts & 1) && (legs & 2))
            total.Add(CommitEffect(view, day.city, all, ground));
        if ((parts & 1) && (legs & 4))
            total.Add(CommitTyped(view, day.city, all, everywhere));
    }
    g_ranOn = day.day;
    g_hash = total.h;
    std::snprintf(g_token, sizeof g_token, " tools6 %08X", g_hash);
}

const DayHook kTools6{"tools6", 58, &Drive};

void Fingerprint(cISC4City*, std::string& line)
{
    line += g_token;
}

const FingerprintPart kFingerprint{"tools6", 19, &Fingerprint};

} // namespace
} // namespace opensc4::autotest
