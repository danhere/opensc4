// The zoning tool's part of the autotest (src/core/autotest_hooks.h;
// tool_zone.cpp): makes the tool the way the city view does, drives it by its
// fields and its vtable slots, and hashes what comes out, so that a run with
// ui.tools.zone on and one with it off can be compared. Only the game's
// addresses and the tool's vtable slots are called, never our own functions.
// OnMouseMove (slot 20) is never called: it stays the game's.
//
//   OPENSC4_TEST_ZONE_TOOL=<day>    token "zonetool", sub-hashes on the log
//     lines "autotest: zonetool: ...", for three tools one after the other
//     (zone type 1 low density residential, 8 medium industry, 0 de-zone):
//
//   1. made: the city view's maker (0x007E6DA0) with the zone type, which
//      runs the constructor (0x004DAE80) and Init (slot 3); the tool's ID,
//      cursor, type, that it is the view's current control, its plain fields;
//   2. prices: three drags set by their fields (+0x40 the press flag, +0x44
//      the start cell, +0x4C the current cell; a free rectangle, a rectangle
//      that overlaps lots, one that crosses a road, found by the lot
//      manager's CanCreateLot), each followed by UpdatePreview (0x004DB240);
//      its return, the ordered rectangle (+0x5C), the number of lots that
//      would go (+0x88), the cursor text and the fields after CancelDrag
//      (slot 27). The price itself goes to the view's cost indicator, which
//      cannot be read back;
//   3. keys: OnKeyDown (slot 14) with Escape in a drag and with another key,
//      OnKeyUp (slot 15), OnMouseExit (slot 22);
//   4. cells: BuildHoverRegion (0x004DAA20) from a free cell, a lot's cell
//      and a road cell (rectangle and bits of the region), CanAddCell
//      (0x004DA850 is its helper, not called) for the same cells;
//   5. tips: SetTipText (0x004DB0D0) for the error codes 0 to 5, once, with
//      the tool of type 0 (the text it leaves);
//   6. away: the view's slot 0x68 puts the tool away (Shutdown, release);
//      the view's current control is null again.
//
//   OPENSC4_TEST_ZONE_TOOL_COMMIT=1 adds two real drags after those: zone
//   type 1 over a free rectangle and the de-zone tool over a rectangle that
//   crosses a road. A drag is set by its fields and let go with OnMouseUpL
//   (slot 18) at a screen point that picks no ground, so the current cell
//   stays as set (the failed pick leaves it, 0x004DBB6C). The return, the
//   fields after and the cursor text are hashed ("commit"); the city's lots
//   and the funds before and after are only logged (the daily fingerprint
//   shows the result).
//
// The press is begun the way OnMouseDownL does (BeginCapture, 0x005FB130, the
// frame timer of the tool's message target) so that CancelDrag and the
// release give them back. Every pointer is left out of the hashes.
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

namespace field { // the tool's fields (tool_zone.cpp)
constexpr std::size_t kInited = 0x04, kId = 0x0C, kCursor = 0x10, kSaved = 0x24, kOverlay = 0x2C, kZones = 0x30;
constexpr std::size_t kLots = 0x34, kSound = 0x38, kType = 0x3C, kDragging = 0x40, kStart = 0x44, kCur = 0x4C;
constexpr std::size_t kHoverTime = 0x54, kHover = 0x58, kRect = 0x5C, kCell = 0x6C, kText = 0x70;
constexpr std::size_t kTextBegin = 0x74, kTextEnd = 0x78, kLotSet = 0x84, kLotCount = 0x88, kPointer = 0x90;
constexpr std::size_t kOutline = 0x94;
constexpr std::size_t kTarget = 0x28;
} // namespace field

constexpr uint32_t kTimerInterval = 0xC0;

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

// The tool's plain fields: no pointers, only whether the held ones are set.
void HashFields(Fnv& f, char* tool)
{
    f.Add(At<uint32_t>(tool, field::kId));
    f.Add(At<uint32_t>(tool, field::kId + 4));
    f.Add(At<uint32_t>(tool, field::kCursor));
    f.Add(At<uint8_t>(tool, field::kInited));
    f.Add(At<uint8_t>(tool, field::kSaved));
    f.Add(At<uint32_t>(tool, field::kType));
    f.Add(At<uint8_t>(tool, field::kDragging));
    for (std::size_t i = 0; i < 4; ++i) {
        f.Add(At<int32_t>(tool, field::kStart + i * 4));
        f.Add(At<int32_t>(tool, field::kRect + i * 4));
    }
    f.Add(At<uint32_t>(tool, field::kHoverTime));
    f.Add(At<uint32_t>(tool, field::kCell));
    f.Add(At<uint32_t>(tool, field::kLotCount));
    const char* const begin = At<const char*>(tool, field::kTextBegin);
    const char* const end = At<const char*>(tool, field::kTextEnd);
    if (begin && end >= begin && end - begin < 256) {
        f.Add(static_cast<uint32_t>(end - begin));
        f.Bytes(begin, static_cast<std::size_t>(end - begin));
    } else {
        f.Add(uint32_t{0xFFFFFFFF});
    }
    for (const std::size_t offset : {field::kOverlay, field::kZones, field::kLots, field::kSound, field::kHover,
                                     field::kLotSet, field::kPointer, field::kOutline})
        f.Add(uint8_t{At<void*>(tool, offset) != nullptr});
}

// A cell region (rectangle at +0, cells at +0x10: width +0x14, depth +0x18,
// the table of rows +0x20; a row is a bit per z): its rectangle and bits;
// returns the number of set cells.
uint32_t HashRegion(Fnv& f, char* region)
{
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

// A drag as OnMouseDownL leaves it: the press flag, the capture, the start
// and current cells, the frame timer of the message target.
void Press(char* tool, int32_t sx, int32_t sz, int32_t ex, int32_t ez)
{
    This<uint8_t>(addr::SC4ViewInputControl_BeginCapture, tool);
    At<uint8_t>(tool, field::kDragging) = 1;
    At<int32_t>(tool, field::kStart) = sx;
    At<int32_t>(tool, field::kStart + 4) = sz;
    At<int32_t>(tool, field::kCur) = ex;
    At<int32_t>(tool, field::kCur + 4) = ez;
    void* const timers = Cdecl<void*>(addr::SC4_GetService86AD10EC);
    Virtual<void>(timers, 0x14, static_cast<void*>(tool + field::kTarget), kTimerInterval);
    At<uint32_t>(tool, field::kHoverTime) = 0;
}

void FreeRegion(void* region)
{
    At<std::uintptr_t>(region, 0x10) = addr::SC4CellRegion_CellsVtable;
    This<void>(addr::SC4CellRegion_DestroyCells, static_cast<char*>(region) + 0x10);
    Cdecl<void>(addr::SC4_operator_delete, region);
}

struct Place {
    bool found = false;
    int32_t x = 0, z = 0;
};

// The first w x h rectangle (scanning rows from the north-west, or from the
// south-east when `fromEnd`, 3 cells in from the edge) to which the lot manager's CanCreateLot (slot 20; no zone,
// no configuration) answers `verdict`: 8 free, 7 a cell some lot has, 3 a
// network, 1 water.
Place Find(void* lots, int32_t sizeX, int32_t sizeZ, int32_t w, int32_t d, int32_t verdict, bool fromEnd = false)
{
    for (int32_t zi = 3; zi + d < sizeZ - 3; ++zi)
        for (int32_t xi = 3; xi + w < sizeX - 3; ++xi) {
            const int32_t x = fromEnd ? sizeX - 1 - xi - w : xi;
            const int32_t z = fromEnd ? sizeZ - 1 - zi - d : zi;
            if (Virtual<int32_t>(lots, 0x50, x, z, w, d, 1u, 0u, 0u) == verdict)
                return {true, x, z};
        }
    return {};
}

struct Spot {
    const char* name;
    int32_t w, d, verdict;
    Place at;
};

void Drive(const Day& day)
{
    if (!OnRunDay(day, "OPENSC4_TEST_ZONE_TOOL") || !game::IsBuild641())
        return;
    char* const view = CityView();
    void* const city = Global<void*>(addr::SC4_City);
    void* const lots = Global<void*>(addr::SC4_LotManager);
    if (!view || !city || !lots) {
        log::Write("autotest: zonetool: no city view");
        return;
    }
    const int32_t sizeX = Virtual<int32_t>(city, 0x174), sizeZ = Virtual<int32_t>(city, 0x178);
    if (sizeX < 64 || sizeZ < 64 || sizeX > 4096 || sizeZ > 4096) {
        log::Write("autotest: zonetool: city size %d x %d", sizeX, sizeZ);
        return;
    }
    const bool commit = EnvInt("OPENSC4_TEST_ZONE_TOOL_COMMIT", 0) != 0;
    cISC4City* const cityObject = static_cast<cISC4City*>(city);

    // The places, found before anything changes: the middle of the city when
    // there is none.
    Spot spots[3] = {{"free", 6, 4, 8, {}}, {"lots", 5, 5, 7, {}}, {"road", 5, 5, 3, {}}};
    for (Spot& spot : spots) {
        spot.at = Find(lots, sizeX, sizeZ, spot.w, spot.d, spot.verdict);
        if (!spot.at.found)
            spot.at = {false, sizeX / 2, sizeZ / 2};
        log::Write("autotest: zonetool: place %s %dx%d at %d,%d (%s)", spot.name, spot.w, spot.d, spot.at.x,
                   spot.at.z, spot.at.found ? "found" : "not found, the middle");
    }
    const Place seeds[3] = {Find(lots, sizeX, sizeZ, 1, 1, 8), Find(lots, sizeX, sizeZ, 1, 1, 7),
                            Find(lots, sizeX, sizeZ, 1, 1, 3)};

    Fnv all;
    static const uint32_t kTypes[3] = {1, 8, 0};
    for (const uint32_t type : kTypes) {
        // --- 1. made, as the city view does (0x007E6DA0) ---------------------------------
        Fnv made;
        made.Add(uint8_t{Virtual<void*>(view, 0x60) != nullptr});
        This<void>(addr::SC4UI_F7E6DA0, view, type);
        char* const tool = Virtual<char*>(view, 0x60);
        if (!tool) {
            log::Write("autotest: zonetool: the view did not take the tool of type %u", type);
            return;
        }
        HashFields(made, tool);
        log::Write("autotest: zonetool: made %u %08X (cursor %08X, cell %08X)", type, made.h,
                   At<uint32_t>(tool, field::kCursor), At<uint32_t>(tool, field::kCell));

        // --- 2. prices -----------------------------------------------------------------
        Fnv prices;
        for (const Spot& spot : spots) {
            Press(tool, spot.at.x, spot.at.z, spot.at.x + spot.w - 1, spot.at.z + spot.d - 1);
            const uint8_t ok = This<uint8_t>(addr::cSC4ViewInputControlZone_UpdatePreview, tool);
            Fnv one;
            one.Add(ok);
            HashFields(one, tool);
            const uint32_t lotCount = At<uint32_t>(tool, field::kLotCount);
            Virtual<void>(tool, 0x6C); // CancelDrag
            HashFields(one, tool);
            prices.Add(one.h);
            log::Write("autotest: zonetool: price %u %s %08X (returned %d, %u lots)", type, spot.name, one.h, ok,
                       lotCount);
        }
        log::Write("autotest: zonetool: prices %u %08X", type, prices.h);

        // --- 3. keys -------------------------------------------------------------------
        Fnv keys;
        const auto step = [&](uint32_t result) {
            keys.Add(result);
            HashFields(keys, tool);
        };
        const Spot& middle = spots[1];
        Press(tool, middle.at.x, middle.at.z, middle.at.x + middle.w - 1, middle.at.z + middle.d - 1);
        This<uint8_t>(addr::cSC4ViewInputControlZone_UpdatePreview, tool);
        step(Virtual<uint8_t>(tool, 0x38, uint32_t{0x1B}, uint32_t{0})); // Escape in a drag
        step(Virtual<uint8_t>(tool, 0x38, uint32_t{0x1B}, uint32_t{0})); // Escape with no drag
        Press(tool, middle.at.x, middle.at.z, middle.at.x + middle.w - 1, middle.at.z + middle.d - 1);
        This<uint8_t>(addr::cSC4ViewInputControlZone_UpdatePreview, tool);
        step(Virtual<uint8_t>(tool, 0x38, uint32_t{0x41}, uint32_t{0})); // another key in a drag
        step(Virtual<uint8_t>(tool, 0x3C, uint32_t{0x41}, uint32_t{0})); // OnKeyUp in a drag
        step(Virtual<uint8_t>(tool, 0x58));                              // OnMouseExit
        Virtual<void>(tool, 0x6C);                                       // CancelDrag
        step(0);
        step(Virtual<uint8_t>(tool, 0x3C, uint32_t{0x41}, uint32_t{0})); // OnKeyUp, no drag
        step(Virtual<uint8_t>(tool, 0x58));
        log::Write("autotest: zonetool: keys %u %08X", type, keys.h);

        // --- 4. cells ------------------------------------------------------------------
        Fnv cells;
        for (const Place& seed : seeds) {
            char* const region = This<char*>(addr::cSC4ViewInputControlZone_BuildHoverRegion, tool, seed.x, seed.z);
            const uint32_t set = HashRegion(cells, region);
            if (region)
                FreeRegion(region);
            struct List {
                void* next;
                void* previous;
            } head = {&head, &head};
            char* list = reinterpret_cast<char*>(&head);
            int32_t cell[2] = {seed.x, seed.z};
            const uint8_t can =
                This<uint8_t>(addr::cSC4ViewInputControlZone_CanAddCell, tool, static_cast<const int32_t*>(cell),
                              static_cast<const int32_t*>(cell), int32_t{8}, &list);
            cells.Add(can);
            log::Write("autotest: zonetool: cells %u at %d,%d: region %d, %u cells, CanAddCell %d", type, seed.x,
                       seed.z, region != nullptr, set, can);
        }
        log::Write("autotest: zonetool: cells %u %08X", type, cells.h);

        // --- 5. tips -------------------------------------------------------------------
        Fnv tips;
        if (type == 0) {
            for (uint32_t error = 1; error <= 5; ++error) {
                This<void>(addr::cSC4ViewInputControlZone_SetTipText, tool, error, static_cast<void*>(tool + field::kText));
                const char* const begin = At<const char*>(tool, field::kTextBegin);
                const char* const end = At<const char*>(tool, field::kTextEnd);
                const uint32_t length = begin && end >= begin ? static_cast<uint32_t>(end - begin) : 0xFFFFFFFFu;
                tips.Add(length);
                if (length < 256)
                    tips.Bytes(begin, length);
                log::Write("autotest: zonetool: tip %u: %u bytes", error, length);
            }
            This<void>(addr::cSC4ViewInputControlZone_SetTipText, tool, uint32_t{0}, static_cast<void*>(tool + field::kText));
            HashFields(tips, tool);
            log::Write("autotest: zonetool: tips %08X", tips.h);
        }

        // --- 6. away, as the city view does ---------------------------------------------
        Virtual<void>(view, 0x68, uint32_t{0});
        Fnv away;
        away.Add(uint8_t{Virtual<void*>(view, 0x60) == nullptr});
        log::Write("autotest: zonetool: away %u %08X (the view's tool is null: %d)", type, away.h,
                   Virtual<void*>(view, 0x60) == nullptr);
        for (const uint32_t h : {made.h, prices.h, keys.h, cells.h, tips.h, away.h})
            all.Add(h);
    }

    // --- the real drags -------------------------------------------------------------------
    if (commit) {
        cISC4LotManager* const lotManager = cityObject->GetLotManager();
        void* const budget = Global<void*>(addr::SC4_BudgetSimulator);
        struct Drag {
            uint32_t type;
            const Spot* spot;
            const char* name;
        };
        // Zoning over free land from the south-east corner; de-zoning the middle
        // half of the city (docs/dezone.md), which has roads in it.
        Spot zoneSpot = {"free", 6, 4, 8, Find(lots, sizeX, sizeZ, 6, 4, 8, true)};
        if (!zoneSpot.at.found)
            zoneSpot.at = {false, sizeX / 2, sizeZ / 2};
        const Spot areaSpot = {"area", sizeX / 2, sizeZ / 2, 0, {true, sizeX / 4, sizeZ / 4}};
        const Drag drags[2] = {{1, &zoneSpot, "zone"}, {0, &areaSpot, "dezone"}};
        for (const Drag& drag : drags) {
            This<void>(addr::SC4UI_F7E6DA0, view, drag.type);
            char* const tool = Virtual<char*>(view, 0x60);
            if (!tool) {
                log::Write("autotest: zonetool: the view did not take the tool of type %u", drag.type);
                return;
            }
            const int32_t lotsBefore = lotManager ? lotManager->GetLotCount() : -1;
            const int64_t fundsBefore = budget ? Virtual<int64_t>(budget, 0x18) : 0;
            const Spot& spot = *drag.spot;
            Press(tool, spot.at.x, spot.at.z, spot.at.x + spot.w - 1, spot.at.z + spot.d - 1);
            This<uint8_t>(addr::cSC4ViewInputControlZone_UpdatePreview, tool);
            const uint8_t taken = Virtual<uint8_t>(tool, 0x48, int32_t{-1000}, int32_t{-1000}, uint32_t{0}); // OnMouseUpL
            Fnv done;
            done.Add(taken);
            HashFields(done, tool);
            const int32_t lotsAfter = lotManager ? lotManager->GetLotCount() : -1;
            const int64_t fundsAfter = budget ? Virtual<int64_t>(budget, 0x18) : 0;
            log::Write("autotest: zonetool: commit %s %08X (returned %d, current cell %d,%d, lots %d -> %d, funds %lld "
                       "-> %lld, lots in the set %u)",
                       drag.name, done.h, taken, At<int32_t>(tool, field::kCur), At<int32_t>(tool, field::kCur + 4),
                       lotsBefore, lotsAfter, static_cast<long long>(fundsBefore),
                       static_cast<long long>(fundsAfter), At<uint32_t>(tool, field::kLotCount));
            all.Add(done.h);
            Virtual<void>(view, 0x68, uint32_t{0});
            all.Add(uint8_t{Virtual<void*>(view, 0x60) == nullptr});
        }
    }
    std::snprintf(g_token, sizeof g_token, " zonetool %08X", all.h);
}

const DayHook kZoneTool{"zonetool", 58, &Drive};

void Fingerprint(cISC4City*, std::string& line)
{
    line += g_token;
}

const FingerprintPart kFingerprint{"zonetool", 13, &Fingerprint};

} // namespace
} // namespace opensc4::autotest
