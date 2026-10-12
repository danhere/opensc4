// The bulldoze tool's part of the autotest (src/core/autotest_hooks.h;
// tool_bulldoze.cpp): makes the tool the way the city view does, drives it
// by its fields and its vtable slots, and hashes what comes out, so that a
// run with ui.tools.bulldoze on and one with it off can be compared. Only
// the game's addresses and the tool's vtable slots are called, never our
// own functions; with game.bulldoze's test switches the same calls go
// through its modes.
//
//   OPENSC4_TEST_BULLDOZE_TOOL=<day>    token "bulldozetool", sub-hashes on
//     the log lines "autotest: bulldozetool: ...":
//
//   1. made: the constructor (0x004B9070, 0xD8 bytes), the view's
//      SetCurrentViewInputControl (slot 0x64) which runs Init (slot 3); the
//      tool's ID, that it is the view's current control, its plain fields
//      and which service pointers are set;
//   2. prices: three drags set by their fields (the press flag +0x44, the
//      hover rectangle +0x48, the start cell +0x58, the end cell +0x60:
//      one cell, a 4x4 block in the middle quarter of the city, a 1x12
//      strip), each followed by Preview (0x004B93B0); the allowed flag
//      (+0x6C), the price (+0x88), the region's rectangle and bit rows
//      (+0x68), the highlighted occupant set or not, and the fields after
//      Cancel (slot 27);
//   3. keys: OnKeyDown (slot 14) with Escape while a press is held, with
//      another key, and with no press; slot 22; Cancel without a press;
//   4. away: the view's slot 0x68 puts the tool away (Shutdown, release);
//      the view's current control is null again.
//
//   OPENSC4_TEST_BULLDOZE_TOOL_COMMIT=1 adds one real drag (the 4x4 block):
//   Preview, then OnMouseUpL (slot 18) at a screen point that picks no
//   ground, so the end cell stays as set (the pick's answer is not looked
//   at). The return, the fields after, are hashed ("commit"); the city's
//   lots and the funds before and after, and the price, are only logged
//   (the daily fingerprint shows the demolished lots).
//
// The press is begun the way OnMouseDownL does (BeginCapture, 0x005FB130) so
// that Cancel and the release give the capture back. Every pointer is left
// out of the hashes.
#include "core/autotest_hooks.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/log.h"

#include <opensc4/addresses.h>

#include <GZServPtrs.h>
#include <cIGZWin.h>
#include <cISC4App.h>
#include <cISC4City.h>
#include <cISC4LotManager.h>
#include <cISC4View3DWin.h>
#include <cRZAutoRefCount.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

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

namespace field { // the tool's fields (tool_bulldoze.cpp)
constexpr std::size_t kInited = 0x04, kId = 0x0C, kBudget = 0x28, kCity = 0x2C, kDemolition = 0x30;
constexpr std::size_t kDeveloper = 0x34, kLots = 0x38, kFilter = 0x3C, kNetwork = 0x40, kDragging = 0x44;
constexpr std::size_t kCell = 0x48, kStart = 0x58, kEnd = 0x60, kRegion = 0x68, kAllowed = 0x6C, kHovered = 0x70;
constexpr std::size_t kLit = 0x74, kSetCount = 0x80, kCost = 0x88, kOverlay = 0x90, kSingle = 0x94, kColors = 0x98;
} // namespace field

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

// The city view, as game_bulldoze_autotest.cpp finds it.
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

// The tool's plain fields after Init and after each step: no pointers, only
// whether the held ones are set.
void HashFields(Fnv& f, char* tool)
{
    f.Add(At<uint32_t>(tool, field::kId));
    f.Add(At<uint32_t>(tool, field::kId + 4));
    f.Add(At<uint8_t>(tool, field::kInited));
    f.Add(At<uint8_t>(tool, field::kDragging));
    for (std::size_t i = 0; i < 4; ++i)
        f.Add(At<int32_t>(tool, field::kCell + i * 4));
    for (const std::size_t offset : {field::kStart, field::kStart + 4, field::kEnd, field::kEnd + 4})
        f.Add(At<int32_t>(tool, offset));
    f.Add(At<uint8_t>(tool, field::kAllowed));
    f.Add(At<uint8_t>(tool, field::kSingle));
    f.Add(At<uint32_t>(tool, field::kSetCount));
    f.Add(At<int64_t>(tool, field::kCost));
    for (const std::size_t offset : {field::kBudget, field::kCity, field::kDemolition, field::kDeveloper,
                                     field::kLots, field::kFilter, field::kNetwork, field::kRegion, field::kHovered,
                                     field::kLit, field::kOverlay})
        f.Add(uint8_t{At<void*>(tool, offset) != nullptr});
}

// The region of the drag (+0x68): its rectangle and the bits inside its
// depth (SC4CellRegion: width +0x14, depth +0x18, rows +0x20); returns the
// number of set cells.
uint32_t HashRegion(Fnv& f, char* tool)
{
    char* const region = At<char*>(tool, field::kRegion);
    if (!region) {
        f.Add(uint8_t{0xEE});
        return 0;
    }
    for (std::size_t i = 0; i < 4; ++i)
        f.Add(At<int32_t>(region, i * 4));
    const uint32_t width = At<uint32_t>(region, 0x14), depth = At<uint32_t>(region, 0x18);
    f.Add(width);
    f.Add(depth);
    uint32_t** const rows = At<uint32_t**>(region, 0x20);
    uint32_t set = 0;
    if (width > 4096 || depth > 4096 || !rows)
        return 0;
    for (uint32_t i = 0; i < width; ++i)
        for (uint32_t j = 0; j < depth; ++j) {
            const uint8_t bit = (rows[i][j >> 5] >> (j & 31)) & 1;
            f.Add(bit);
            set += bit;
        }
    return set;
}

// Begins a press the way OnMouseDownL does, at the given cells.
void Press(char* tool, int32_t sx, int32_t sz, int32_t ex, int32_t ez)
{
    This<uint8_t>(addr::SC4ViewInputControl_BeginCapture, tool);
    At<uint8_t>(tool, field::kDragging) = 1;
    At<int32_t>(tool, field::kCell) = sx;
    At<int32_t>(tool, field::kCell + 4) = sz;
    At<int32_t>(tool, field::kCell + 8) = sx;
    At<int32_t>(tool, field::kCell + 12) = sz;
    At<int32_t>(tool, field::kStart) = sx;
    At<int32_t>(tool, field::kStart + 4) = sz;
    At<int32_t>(tool, field::kEnd) = ex;
    At<int32_t>(tool, field::kEnd + 4) = ez;
}

struct Drag {
    int32_t sx, sz, ex, ez;
    const char* name;
};

void Drive(const Day& day)
{
    if (!OnRunDay(day, "OPENSC4_TEST_BULLDOZE_TOOL") || !game::IsBuild641())
        return;
    char* const view = CityView();
    void* const city = Global<void*>(addr::SC4_City);
    if (!view || !city) {
        log::Write("autotest: bulldozetool: no city view");
        return;
    }
    int32_t sizeX = Virtual<int32_t>(city, 0x174), sizeZ = Virtual<int32_t>(city, 0x178);
    if (sizeX < 64 || sizeZ < 64 || sizeX > 4096 || sizeZ > 4096) {
        log::Write("autotest: bulldozetool: city size %d x %d", sizeX, sizeZ);
        return;
    }
    const bool commit = EnvInt("OPENSC4_TEST_BULLDOZE_TOOL_COMMIT", 0) != 0;
    cISC4City* const cityObject = static_cast<cISC4City*>(city);

    // --- 1. made, as the city view does (SetTool) ----------------------------------------
    Fnv made;
    void* const before = Virtual<void*>(view, 0x60);
    made.Add(uint8_t{before != nullptr});
    char* const tool = static_cast<char*>(Cdecl<void*>(addr::SC4_operator_new, 0xD8u));
    if (!tool || !This<void*>(addr::SC4ViewInputControls_F4B9070, tool)) {
        log::Write("autotest: bulldozetool: no tool");
        return;
    }
    Virtual<uint32_t>(tool, 0x4); // AddRef: the view's SetTool holds one while it sets the tool
    Virtual<uint8_t>(view, 0x64, static_cast<void*>(tool), int32_t{0});
    Virtual<uint32_t>(tool, 0x8);
    const bool current = Virtual<void*>(view, 0x60) == tool;
    made.Add(uint8_t{current});
    if (!current) {
        log::Write("autotest: bulldozetool: the view did not take the tool");
        return;
    }
    HashFields(made, tool);
    for (std::size_t i = 0; i < 16; ++i)
        made.Add(At<uint32_t>(tool, field::kColors + i * 4));
    log::Write("autotest: bulldozetool: made %08X (view had a tool: %d)", made.h, before != nullptr);

    // --- 2. prices ------------------------------------------------------------------------
    const Drag drags[3] = {
        {sizeX / 2, sizeZ / 2, sizeX / 2, sizeZ / 2, "cell"},
        {sizeX * 3 / 8, sizeZ * 3 / 8, sizeX * 3 / 8 + 3, sizeZ * 3 / 8 + 3, "block"},
        {sizeX / 2, sizeZ / 4, sizeX / 2, sizeZ / 4 + 11, "strip"},
    };
    Fnv prices;
    for (const Drag& drag : drags) {
        Press(tool, drag.sx, drag.sz, drag.ex, drag.ez);
        This<void>(addr::cSC4ViewInputControlDemolish_Preview, tool);
        Fnv one;
        HashFields(one, tool);
        const uint32_t cells = HashRegion(one, tool);
        Virtual<void>(tool, 0x6C); // Cancel
        HashFields(one, tool);
        prices.Add(one.h);
        log::Write("autotest: bulldozetool: price %s %08X (allowed %d, cost %lld, %u cells)", drag.name, one.h,
                   At<uint8_t>(tool, field::kAllowed), static_cast<long long>(At<int64_t>(tool, field::kCost)),
                   cells);
    }

    // --- 3. keys ---------------------------------------------------------------------------
    Fnv keys;
    const auto step = [&](uint32_t result) {
        keys.Add(result);
        HashFields(keys, tool);
    };
    Press(tool, drags[1].sx, drags[1].sz, drags[1].ex, drags[1].ez);
    This<void>(addr::cSC4ViewInputControlDemolish_Preview, tool);
    step(Virtual<uint8_t>(tool, 0x38, uint32_t{0x1B}, uint32_t{0})); // Escape in a press
    step(Virtual<uint8_t>(tool, 0x38, uint32_t{0x1B}, uint32_t{0})); // Escape with no press
    Press(tool, drags[2].sx, drags[2].sz, drags[2].ex, drags[2].ez);
    This<void>(addr::cSC4ViewInputControlDemolish_Preview, tool);
    step(Virtual<uint8_t>(tool, 0x38, uint32_t{0x41}, uint32_t{0})); // another key: the press stays
    step(Virtual<uint8_t>(tool, 0x58));                              // slot 22
    Virtual<void>(tool, 0x6C);                                       // Cancel: lets the press go
    step(0);
    Virtual<void>(tool, 0x6C); // Cancel with no press
    step(1);
    log::Write("autotest: bulldozetool: keys %08X", keys.h);

    // --- the one real drag -----------------------------------------------------------------
    Fnv done;
    if (commit) {
        cISC4LotManager* const lots = cityObject->GetLotManager();
        void* const budget = Global<void*>(addr::SC4_BudgetSimulator);
        const int32_t lotsBefore = lots ? lots->GetLotCount() : -1;
        const int64_t fundsBefore = budget ? Virtual<int64_t>(budget, 0x18) : 0;
        Press(tool, drags[1].sx, drags[1].sz, drags[1].ex, drags[1].ez);
        This<void>(addr::cSC4ViewInputControlDemolish_Preview, tool);
        const uint8_t taken = Virtual<uint8_t>(tool, 0x48, int32_t{-1000}, int32_t{-1000}, uint32_t{0}); // OnMouseUpL
        done.Add(taken);
        HashFields(done, tool);
        const int32_t lotsAfter = lots ? lots->GetLotCount() : -1;
        const int64_t fundsAfter = budget ? Virtual<int64_t>(budget, 0x18) : 0;
        log::Write("autotest: bulldozetool: commit %08X (returned %d, end cell %d,%d, lots %d -> %d, funds %lld -> %lld, "
                   "cost %lld, allowed %d)",
                   done.h, taken, At<int32_t>(tool, field::kEnd), At<int32_t>(tool, field::kEnd + 4), lotsBefore,
                   lotsAfter, static_cast<long long>(fundsBefore), static_cast<long long>(fundsAfter),
                   static_cast<long long>(At<int64_t>(tool, field::kCost)), At<uint8_t>(tool, field::kAllowed));
    }

    // --- 4. away, as the city view does -----------------------------------------------------
    Virtual<void>(view, 0x68, uint32_t{0});
    Fnv away;
    away.Add(uint8_t{Virtual<void*>(view, 0x60) == nullptr});
    log::Write("autotest: bulldozetool: away %08X (the view's tool is null: %d)", away.h,
               Virtual<void*>(view, 0x60) == nullptr);

    Fnv all;
    for (const uint32_t h : {made.h, prices.h, keys.h, away.h})
        all.Add(h);
    if (commit)
        all.Add(done.h);
    std::snprintf(g_token, sizeof g_token, " bulldozetool %08X", all.h);
}

const DayHook kBulldozeTool{"bulldozetool", 58, &Drive};

void Fingerprint(cISC4City*, std::string& line)
{
    line += g_token;
}

const FingerprintPart kFingerprint{"bulldozetool", 12, &Fingerprint};

} // namespace
} // namespace opensc4::autotest
