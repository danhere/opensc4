// The plop tool's part of the autotest (src/core/autotest_hooks.h;
// tool_plop_1.cpp, tool_plop_2.cpp, tool_plop_3.cpp): makes the tool the way
// the city view's tool makers do, drives it by its fields and its vtable
// slots, and hashes what comes out, so that a run with ui.tools.plop1 to
// ui.tools.plop3 on and one with them off can be compared. Only the game's
// addresses and the tool's vtable slots are called, never our own functions.
//
//   OPENSC4_TEST_PLOP_TOOL=<day>    token "ploptool", sub-hashes on the log
//     lines "autotest: ploptool: ...":
//
//   1. the lot configuration: the first lot (scanning the cells from the
//      north-west) whose configuration is known to the lot configuration
//      manager and fits in 6 x 6 cells; its ID is logged. A tool with a
//      configuration ID never opens a chooser window (Init asks for one only
//      without an ID);
//   2. made: the tool as the city view makes it (constructor 0x004C0430 with
//      mode 0, the configuration ID, no building, accept-all 0; held; taken
//      by the view's slot 0x64), which runs Init; its plain fields;
//   3. checks: for a free place, a place on lots and a place off the map or
//      on water (found by the lot manager's CanCreateLot with the
//      configuration), each of the four rotations: SnapFootprint
//      (0x004BE750), FitRotation (0x004BEB70), PriceDemolition (0x004BEAB0)
//      and TestPlacement (0x004C1110): their returns, rectangles, snapped
//      points, prices, verdicts and refusal texts;
//   4. handlers: OnKeyDown (slot 14) with some keys (which of them rotate
//      depends on the game's key accelerator), the message target's DoMessage
//      (slot 3 at +0x28) with three messages that open no window, OnMouseExit
//      (slot 22);
//   5. away: the view's slot 0x68 puts the tool away (Shutdown, destructor);
//      the view's current control is null again.
//
//   OPENSC4_TEST_PLOP_TOOL_COMMIT=1 adds a real placement: a new tool, the
//   mouse moved (slot 20) over a grid of screen points in a fixed order until
//   the tool says the lot may go (+0x94), then OnMouseDownL (slot 16) at that
//   point, which picks the terrain again and asks PlaceLot. The point, the
//   return and the fields after are hashed; the lot count and the funds
//   before and after are only logged (the daily fingerprint shows the rest).
//   A tutorial city holds the placing back (OPENSC4_TEST_TUTORIAL_OFF=1).
//
// Every pointer is left out of the hashes.
#include "core/autotest_hooks.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/log.h"

#include <opensc4/addresses.h>

#include <GZServPtrs.h>
#include <cIGZMessage2Standard.h>
#include <cIGZWin.h>
#include <cISC4App.h>
#include <cISC4City.h>
#include <cISC4Lot.h>
#include <cISC4LotConfiguration.h>
#include <cISC4LotManager.h>
#include <cISC4View3DWin.h>
#include <cRZAutoRefCount.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
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

namespace field { // the tool's fields (tool_plop_1.cpp to tool_plop_3.cpp)
constexpr std::size_t kInited = 0x04, kIdA = 0x0C, kIdB = 0x10, kMode = 0x2C, kConfigId = 0x30, kKeyId = 0x34;
constexpr std::size_t kRotationWanted = 0x38, kRotation = 0x3C, kRotationSeen = 0x40;
constexpr std::size_t kMouseX = 0x44, kMouseZ = 0x48, kConfigManager = 0x4C, kIdSetCount = 0x64;
constexpr std::size_t kDevelopment = 0x68, kBounds = 0x78, kPoint = 0x88, kAllowed = 0x94, kShown = 0x95;
constexpr std::size_t kTextBegin = 0x9C, kTextEnd = 0xA0, kHolder = 0xAC, kEffect = 0xD8, kOverlayA = 0xDC;
constexpr std::size_t kOverlayB = 0xE0, kMarker = 0x118, kBaseCost = 0x128, kCostCache = 0x138, kCursor = 0x140;
constexpr std::size_t kSpecial = 0x150, kPlacementsLeft = 0x154;
constexpr std::size_t kMessageTarget = 0x28;
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

// The city view, as tool_zone_autotest.cpp finds it.
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
    for (const std::size_t offset :
         {field::kIdA, field::kIdB, field::kMode, field::kConfigId, field::kKeyId, field::kRotationWanted,
          field::kRotation, field::kRotationSeen, field::kMouseX, field::kMouseZ, field::kIdSetCount, field::kCursor,
          field::kPlacementsLeft})
        f.Add(At<uint32_t>(tool, offset));
    f.Add(At<uint8_t>(tool, field::kInited));
    f.Add(At<uint8_t>(tool, field::kAllowed));
    f.Add(At<uint8_t>(tool, field::kShown));
    f.Add(At<uint8_t>(tool, field::kMarker));
    f.Add(At<uint8_t>(tool, field::kSpecial));
    for (std::size_t i = 0; i < 4; ++i)
        f.Add(At<int32_t>(tool, field::kBounds + i * 4));
    for (std::size_t i = 0; i < 3; ++i)
        f.Add(At<uint32_t>(tool, field::kPoint + i * 4));
    f.Add(At<int64_t>(tool, field::kBaseCost));
    f.Add(At<int64_t>(tool, field::kCostCache));
    const char* const begin = At<const char*>(tool, field::kTextBegin);
    const char* const end = At<const char*>(tool, field::kTextEnd);
    if (begin && end >= begin && end - begin < 256) {
        f.Add(static_cast<uint32_t>(end - begin));
        f.Bytes(begin, static_cast<std::size_t>(end - begin));
    } else {
        f.Add(uint32_t{0xFFFFFFFF});
    }
    for (const std::size_t offset : {field::kConfigManager, field::kDevelopment, field::kHolder, field::kEffect,
                                     field::kOverlayA, field::kOverlayB})
        f.Add(uint8_t{At<void*>(tool, offset) != nullptr});
}

// A game string (0x14 bytes) with an 8-byte buffer, as the tool's callers
// make one for TestPlacement's refusal text.
struct Text {
    std::uintptr_t vtable;
    char* begin;
    char* end;
    char* capacity;
    uint32_t references;
};

void StartText(Text& text)
{
    char* const buffer = Cdecl<char*>(addr::SC4_stl_allocate, uint32_t{8});
    buffer[0] = 0;
    text.vtable = addr::SC4String_Vtable;
    text.begin = buffer;
    text.end = buffer;
    text.capacity = buffer + 8;
    text.references = 0;
}

void HashText(Fnv& f, Text& text)
{
    const std::size_t length = text.begin && text.end >= text.begin ? static_cast<std::size_t>(text.end - text.begin) : 0;
    f.Add(static_cast<uint32_t>(length));
    if (length < 512)
        f.Bytes(text.begin, length);
    if (text.begin)
        Cdecl<void>(addr::SC4_Free, static_cast<void*>(text.begin));
}

// A message of the given type to the tool's message target.
void Send(char* tool, uint32_t type)
{
    using NewFn = void*(__cdecl*)(std::size_t size);
    using MessageFn = cIGZMessage2Standard*(__thiscall*)(void* memory);
    using DoMessageFn = bool(__thiscall*)(void* target, cIGZMessage2* message);
    void* const memory = reinterpret_cast<NewFn>(addr::RZ_operator_new)(0x2C);
    cIGZMessage2Standard* const message =
        memory ? reinterpret_cast<MessageFn>(addr::cGZMessage_cGZMessage)(memory) : nullptr;
    if (!message)
        return;
    message->AddRef();
    message->SetType(type);
    void* const target = tool + field::kMessageTarget;
    reinterpret_cast<DoMessageFn>((*static_cast<void***>(target))[3])(target, message);
    message->Release();
}

// The tool as the city view makes it (0x007E9FA0, message 0x6A2B245F, without
// its mouse move): the tool held, given to the view, the view's current
// control. Null when the view did not take it.
char* MakeTool(char* view, uint32_t configuration)
{
    void* const memory = Cdecl<void*>(addr::SC4_operator_new, uint32_t{0x158});
    void* const made =
        memory ? This<void*>(addr::SC4ViewInputControls_F4C0430, memory, uint32_t{0}, configuration, uint32_t{0}, uint32_t{0})
               : nullptr;
    if (!made)
        return nullptr;
    void* held = nullptr;
    This<void*>(addr::SC4RefHolder_SC4RefHolder, &held, made);
    Virtual<uint8_t>(view, 0x64, held, int32_t{2});
    Virtual<uint32_t>(held, 0x8); // let go of the holder's reference; the view has its own
    return Virtual<char*>(view, 0x60);
}

struct Place {
    bool found = false;
    int32_t x = 0, z = 0;
};

// The first w x d rectangle (rows from the north-west, 3 cells in from the
// edge) to which the lot manager's CanCreateLot answers `verdict` for the
// configuration: 8 free, 7 a cell some lot has, 3 a network, 1 water.
Place Find(void* lots, void* configuration, int32_t sizeX, int32_t sizeZ, int32_t w, int32_t d, int32_t verdict)
{
    for (int32_t zi = 3; zi + d < sizeZ - 3; ++zi)
        for (int32_t xi = 3; xi + w < sizeX - 3; ++xi)
            if (Virtual<int32_t>(lots, 0x50, xi, zi, w, d, 1u, 0u, configuration) == verdict)
                return {true, xi, zi};
    return {};
}

struct Spot {
    const char* name;
    int32_t verdict;
    Place at;
};

void Drive(const Day& day)
{
    if (!OnRunDay(day, "OPENSC4_TEST_PLOP_TOOL") || !game::IsBuild641())
        return;
    char* const view = CityView();
    void* const city = Global<void*>(addr::SC4_City);
    void* const lots = Global<void*>(addr::SC4_LotManager);
    void* const manager = Global<void*>(addr::SC4_Service43D18);
    void* const terrain = Global<void*>(addr::SC4_Terrain);
    if (!view || !city || !lots || !manager || !terrain) {
        log::Write("autotest: ploptool: no city view");
        return;
    }
    const int32_t sizeX = Virtual<int32_t>(city, 0x174), sizeZ = Virtual<int32_t>(city, 0x178);
    if (sizeX < 64 || sizeZ < 64 || sizeX > 4096 || sizeZ > 4096) {
        log::Write("autotest: ploptool: city size %d x %d", sizeX, sizeZ);
        return;
    }
    const bool commit = EnvInt("OPENSC4_TEST_PLOP_TOOL_COMMIT", 0) != 0;
    cISC4LotManager* const lotManager = static_cast<cISC4City*>(city)->GetLotManager();

    // --- 1. the lot configuration --------------------------------------------------------
    uint32_t configurationId = 0;
    uint32_t width = 0, depth = 0;
    for (int32_t z = 0; z < sizeZ && !configurationId; ++z)
        for (int32_t x = 0; x < sizeX && !configurationId; ++x) {
            cISC4Lot* const lot = lotManager->GetLot(x, z, false);
            cISC4LotConfiguration* const configuration = lot ? lot->GetLotConfiguration() : nullptr;
            if (!configuration)
                continue;
            const uint32_t id = configuration->GetID();
            uint32_t w = 0, d = 0;
            configuration->GetSize(w, d);
            if (id && w >= 1 && d >= 1 && w <= 6 && d <= 6 && Virtual<void*>(manager, 0x20, id) == configuration) {
                configurationId = id;
                width = w;
                depth = d;
            }
        }
    if (!configurationId) {
        log::Write("autotest: ploptool: no lot configuration found");
        std::snprintf(g_token, sizeof g_token, " ploptool none");
        return;
    }
    void* const configuration = Virtual<void*>(manager, 0x20, configurationId);
    log::Write("autotest: ploptool: configuration %08X, %ux%u cells", configurationId, width, depth);

    Spot spots[3] = {{"free", 8, {}}, {"lots", 7, {}}, {"water", 1, {}}};
    for (Spot& spot : spots) {
        spot.at = Find(lots, configuration, sizeX, sizeZ, static_cast<int32_t>(width), static_cast<int32_t>(depth),
                       spot.verdict);
        if (!spot.at.found)
            spot.at = {false, spot.verdict == 1 ? -2 : sizeX / 2, spot.verdict == 1 ? -2 : sizeZ / 2};
        log::Write("autotest: ploptool: place %s at %d,%d (%s)", spot.name, spot.at.x, spot.at.z,
                   spot.at.found ? "found" : "not found");
    }

    Fnv all;
    {
        // --- 2. made --------------------------------------------------------------------
        Fnv made;
        made.Add(uint8_t{Virtual<void*>(view, 0x60) != nullptr});
        char* const tool = MakeTool(view, configurationId);
        if (!tool) {
            log::Write("autotest: ploptool: the view did not take the tool");
            return;
        }
        HashFields(made, tool);
        log::Write("autotest: ploptool: made %08X (mode %u, configuration %08X, building %08X, base cost %lld, "
                   "placements left %d, rotation %u)",
                   made.h, At<uint32_t>(tool, field::kMode), At<uint32_t>(tool, field::kConfigId),
                   At<uint32_t>(tool, field::kKeyId), static_cast<long long>(At<int64_t>(tool, field::kBaseCost)),
                   At<int32_t>(tool, field::kPlacementsLeft), At<uint32_t>(tool, field::kRotation));

        // --- 3. checks --------------------------------------------------------------------
        Fnv checks;
        const double cell = Virtual<double>(terrain, 0x18);
        for (const Spot& spot : spots) {
            Fnv one;
            const float box[3] = {static_cast<float>((spot.at.x + width * 0.5) * cell), 0.0f,
                                  static_cast<float>((spot.at.z + depth * 0.5) * cell)};
            for (int32_t rotation = 0; rotation < 4; ++rotation) {
                int32_t rect[4] = {-7, -7, -7, -7};
                float snapped[3] = {box[0], box[1], box[2]};
                const uint8_t snap = This<uint8_t>(addr::cSC4ViewInputControlPlaceLot_SnapFootprint, tool, rotation,
                                                   box[0], box[2], &snapped[0], &snapped[2], rect);
                one.Add(snap);
                for (const int32_t value : rect)
                    one.Add(value);
                one.Add(snapped[0]);
                one.Add(snapped[2]);

                int64_t price = 0x7777;
                const uint8_t priced = This<uint8_t>(addr::cSC4ViewInputControlPlaceLot_PriceDemolition, tool,
                                                     configuration, box, rotation, rect, &price);
                one.Add(priced);
                one.Add(price);

                int32_t fitRotation = -7;
                int32_t fitRect[4] = {-7, -7, -7, -7};
                const uint8_t fit = This<uint8_t>(addr::cSC4ViewInputControlPlaceLot_FitRotation, tool, configuration,
                                                  box, &fitRotation, fitRect);
                one.Add(fit);
                one.Add(fitRotation);
                for (const int32_t value : fitRect)
                    one.Add(value);

                int32_t testRect[4] = {-7, -7, -7, -7};
                uint8_t ok = 0xEE;
                int64_t cost = 0x7777;
                int32_t code = -7;
                Text reason;
                StartText(reason);
                const uint8_t fits = This<uint8_t>(addr::cSC4ViewInputControlPlaceLot_TestPlacement, tool, box,
                                                   static_cast<uint32_t>(rotation), testRect, &ok, &cost, &code,
                                                   &reason);
                one.Add(fits);
                one.Add(ok);
                one.Add(cost);
                one.Add(code);
                for (const int32_t value : testRect)
                    one.Add(value);
                HashText(one, reason);
                log::Write("autotest: ploptool: check %s rotation %d: snap %d, price %d (%lld), fit %d (rotation %d), "
                           "test %d (ok %d, cost %lld, verdict %d)",
                           spot.name, rotation, snap, priced, static_cast<long long>(price), fit, fitRotation, fits,
                           ok, static_cast<long long>(cost), code);
            }
            checks.Add(one.h);
            log::Write("autotest: ploptool: checks %s %08X", spot.name, one.h);
        }
        HashFields(checks, tool);
        log::Write("autotest: ploptool: checks %08X", checks.h);

        // --- 4. handlers ------------------------------------------------------------------
        Fnv handlers;
        int rotated = 0;
        for (const int32_t key : {0x25, 0x27, 0x52, 0x51, 0x45, 0xDB, 0xDD, 0x41, 0x21, 0x22, 0xBC, 0xBE, 0x20, 0x09}) {
            const uint32_t before = At<uint32_t>(tool, field::kRotation);
            const uint8_t taken = Virtual<uint8_t>(tool, 0x38, key, uint32_t{0});
            handlers.Add(taken);
            HashFields(handlers, tool);
            rotated += At<uint32_t>(tool, field::kRotation) != before;
        }
        for (const uint32_t type : {0x49D5B83Fu, 0x277A9364u, 0xA6B79602u, 0x12345678u}) {
            Send(tool, type);
            HashFields(handlers, tool);
        }
        handlers.Add(Virtual<uint8_t>(tool, 0x58)); // OnMouseExit
        HashFields(handlers, tool);
        log::Write("autotest: ploptool: handlers %08X (%d keys turned the lot)", handlers.h, rotated);

        // --- 5. away, as the city view does -------------------------------------------------
        Virtual<void>(view, 0x68, uint32_t{0});
        Fnv away;
        away.Add(uint8_t{Virtual<void*>(view, 0x60) == nullptr});
        log::Write("autotest: ploptool: away %08X (the view's tool is null: %d)", away.h,
                   Virtual<void*>(view, 0x60) == nullptr);
        for (const uint32_t h : {made.h, checks.h, handlers.h, away.h})
            all.Add(h);
    }

    // --- the real placement ------------------------------------------------------------------
    if (commit) {
        void* const budget = Global<void*>(addr::SC4_BudgetSimulator);
        char* const tool = MakeTool(view, configurationId);
        if (!tool) {
            log::Write("autotest: ploptool: the view did not take the tool (commit)");
            return;
        }
        Virtual<uint32_t>(tool, 0x4); // our own reference: the tool may end itself
        int32_t found = -1, hx = 0, hz = 0, tries = 0;
        for (int32_t z = 60; z <= 700 && found < 0; z += 40)
            for (int32_t x = 40; x <= 980 && found < 0; x += 60) {
                ++tries;
                Virtual<uint8_t>(tool, 0x50, x, z, uint32_t{0}); // OnMouseMove
                if (At<uint8_t>(tool, field::kAllowed)) {
                    found = 1;
                    hx = x;
                    hz = z;
                }
            }
        Fnv done;
        done.Add(found);
        done.Add(hx);
        done.Add(hz);
        HashFields(done, tool);
        log::Write("autotest: ploptool: hover %08X (%s at %d,%d after %d tries, point %08X %08X %08X)", done.h,
                   found >= 0 ? "allowed" : "never allowed", hx, hz, tries, At<uint32_t>(tool, field::kPoint),
                   At<uint32_t>(tool, field::kPoint + 4), At<uint32_t>(tool, field::kPoint + 8));
        const int32_t lotsBefore = lotManager ? lotManager->GetLotCount() : -1;
        const int64_t fundsBefore = budget ? Virtual<int64_t>(budget, 0x18) : 0;
        Fnv click;
        if (found >= 0) {
            const uint8_t placed = Virtual<uint8_t>(tool, 0x40, hx, hz, uint32_t{0}); // OnMouseDownL
            click.Add(placed);
            HashFields(click, tool);
            const int32_t lotsAfter = lotManager ? lotManager->GetLotCount() : -1;
            const int64_t fundsAfter = budget ? Virtual<int64_t>(budget, 0x18) : 0;
            log::Write("autotest: ploptool: commit %08X (returned %d, lots %d -> %d, funds %lld -> %lld, placements left "
                       "%d, the view still has the tool: %d)",
                       click.h, placed, lotsBefore, lotsAfter, static_cast<long long>(fundsBefore),
                       static_cast<long long>(fundsAfter), At<int32_t>(tool, field::kPlacementsLeft),
                       Virtual<void*>(view, 0x60) == tool);
        }
        Virtual<uint32_t>(tool, 0x8); // our reference
        Virtual<void>(view, 0x68, uint32_t{0});
        const bool away = Virtual<void*>(view, 0x60) == nullptr;
        log::Write("autotest: ploptool: away after the commit (the view's tool is null: %d)", away);
        all.Add(done.h);
        all.Add(click.h);
        all.Add(uint8_t{away});
    }
    std::snprintf(g_token, sizeof g_token, " ploptool %08X", all.h);
}

const DayHook kPlopTool{"ploptool", 59, &Drive};

void Fingerprint(cISC4City*, std::string& line)
{
    line += g_token;
}

const FingerprintPart kFingerprint{"ploptool", 14, &Fingerprint};

} // namespace
} // namespace opensc4::autotest
