// A test that reaches the two functions of the city view window's drawing
// that nothing else reaches (docs/renderer.md, "The city view window"): the
// cost indicator's amount (0x007EAC70) and SetScrolling (0x007F64F0). Both
// are called through the view's interface, as a tool or the edge scrolling
// calls them: SetCostIndicator with two numbers (slot 42) and
// ClearCostIndicator (slot 44) draw the amount whenever the indicator's
// state changes, and SetScrolling is slot 33.
//
// `renderer.cityview.draw_untested` has no comparison of its own for these
// two, so the token is the check: the amount's picture after every call and
// the view's scroll fields, the same in a run with the replacement on as in
// a run of the game alone.
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

#include <cstdint>
#include <cstdio>
#include <string>

namespace opensc4::autotest {
namespace {

// The city view, as autotest.cpp finds it for screenshots.
char* View()
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

// The view's fields (src/core/ui_cityview_3.cpp).
constexpr std::size_t kCost = 0x3B0;
constexpr std::size_t kCostState = 0x3B4;
constexpr std::size_t kCostKind = 0x3B8;
constexpr std::size_t kCostPicture = 0x3D0; // the buffer the amount is drawn into
constexpr std::size_t kScrollAngle = 0x328;
constexpr std::size_t kScrollSpeed = 0x32C;
constexpr std::size_t kScrolling = 0x330;
constexpr std::size_t kScrollByte = 0x339;

struct Fnv {
    uint32_t h = 2166136261u;
    void Add(uint32_t value)
    {
        for (int i = 0; i < 4; ++i)
            h = (h ^ ((value >> (8 * i)) & 0xFF)) * 16777619u;
    }
};

// A picture buffer's memory as a checksum; 0 when it cannot be read.
uint32_t Sum(void* picture)
{
    if (!picture)
        return 0;
    const uint8_t* const bits = Virtual<const uint8_t*>(picture, 0x88);
    const uint32_t stride = Virtual<uint32_t>(picture, 0x8C), height = Virtual<uint32_t>(picture, 0x28);
    if (!bits || !stride || !height || height > 4096 || stride > 4096 * 4)
        return 0;
    uint32_t h = 2166136261u;
    for (std::size_t i = 0; i < std::size_t{stride} * height; ++i)
        h = (h ^ bits[i]) * 16777619u;
    return h;
}

std::string g_token;

// OPENSC4_TEST_CITY_DRAW=<day>: on that run day the cost indicator is set
// five times through the view's interface (a small amount, the same again,
// a larger one, one too dear, a small one again), faded and switched off,
// and the scrolling is started and stopped in the same tick (with a second
// start while scrolling and a second stop while not, which only store the
// angle and the speed). The token says how many calls should have drawn the
// amount (those that changed the indicator's state) and how many changed
// the picture, a checksum of the picture and the indicator's fields after
// every call, and one of the scroll fields after each of the four
// SetScrolling calls.
void DriveCityDraw(const Day& day)
{
    if (!OnRunDay(day, "OPENSC4_TEST_CITY_DRAW") || !game::IsBuild641())
        return;
    char* const view = View();
    if (!view) {
        g_token = " | cvdraw none";
        log::Write("autotest: city view drawing: no view");
        return;
    }

    Fnv cost;
    int due = 0, drew = 0, changed = 0;
    const auto noted = [&](const char* what, uint32_t stateBefore, uint32_t kindBefore, uint32_t costBefore, uint32_t before) {
        const uint32_t after = Sum(At<void*>(view, kCostPicture));
        // The amount is drawn when the call changes what the indicator shows.
        const bool draws = At<uint32_t>(view, kCostState) != 0 &&
                           (stateBefore != At<uint32_t>(view, kCostState) || kindBefore != At<uint32_t>(view, kCostKind) ||
                            costBefore != At<uint32_t>(view, kCost));
        due += draws;
        drew += draws && after != 0;
        changed += after != before;
        cost.Add(after);
        cost.Add(At<uint32_t>(view, kCostState));
        cost.Add(At<uint32_t>(view, kCostKind));
        cost.Add(At<uint32_t>(view, kCost));
        log::Write("autotest: city view drawing: %s: state %u, kind %u, cost %u, picture %08X%s", what, At<uint32_t>(view, kCostState),
                   At<uint32_t>(view, kCostKind), At<uint32_t>(view, kCost), after, after != before ? " (changed)" : "");
    };
    const auto set = [&](uint32_t amount, uint32_t price) {
        const uint32_t s = At<uint32_t>(view, kCostState), k = At<uint32_t>(view, kCostKind), c = At<uint32_t>(view, kCost);
        const uint32_t before = Sum(At<void*>(view, kCostPicture));
        Virtual<uint32_t>(view, 0xA8, amount, price); // SetCostIndicator(cost, price)
        noted("set", s, k, c, before);
    };
    const auto clear = [&](uint32_t fade) {
        const uint32_t s = At<uint32_t>(view, kCostState), k = At<uint32_t>(view, kCostKind), c = At<uint32_t>(view, kCost);
        const uint32_t before = Sum(At<void*>(view, kCostPicture));
        Virtual<uint32_t>(view, 0xB0, fade); // ClearCostIndicator(fade)
        noted(fade ? "fade" : "off", s, k, c, before);
    };
    if (At<void*>(view, kCostPicture) && At<void*>(view, 0x3D4)) {
        clear(0);
        set(125, 0);
        set(125, 0);               // nothing changed: not drawn again
        set(98765, 0);
        set(0x7FFFFFFF, 0x7FFFFFFF); // dearer than any city's funds
        set(7, 0);
        clear(1);
        clear(0);
    } else {
        log::Write("autotest: city view drawing: the view has no cost indicator");
    }

    // SetScrolling(on, angle, speed): started and stopped before a frame
    // can pass, so the view does not move.
    Fnv scroll;
    int calls = 0;
    const auto scrolling = [&](uint32_t on, float angle, float speed) {
        Virtual<void>(view, 0x84, on, angle, speed);
        ++calls;
        scroll.Add(At<uint32_t>(view, kScrollAngle));
        scroll.Add(At<uint32_t>(view, kScrollSpeed));
        scroll.Add(At<uint8_t>(view, kScrolling));
        scroll.Add(At<uint8_t>(view, kScrollByte));
        log::Write("autotest: city view drawing: SetScrolling(%u): angle %08X, speed %08X, scrolling %u, byte %u", on,
                   At<uint32_t>(view, kScrollAngle), At<uint32_t>(view, kScrollSpeed), At<uint8_t>(view, kScrolling),
                   At<uint8_t>(view, kScrollByte));
    };
    if (!At<uint8_t>(view, kScrolling)) {
        scrolling(1, 0.5f, 1.0f);
        scrolling(1, 1.5f, 2.0f); // scrolling already: only the angle and the speed
        scrolling(0, 0.0f, 0.0f);
        scrolling(0, 0.25f, 0.0f); // not scrolling: only the fields
    } else {
        log::Write("autotest: city view drawing: the view is scrolling; SetScrolling left alone");
    }

    char text[160];
    std::snprintf(text, sizeof text, " | cvdraw cost due %d drew %d changed %d %08X scroll %d %08X", due, drew, changed, cost.h, calls,
                  scroll.h);
    g_token = text;
    log::Write("autotest: city view drawing driven:%s", text);
}

void Fingerprint(cISC4City*, std::string& line)
{
    line += g_token;
}

const FingerprintPart kFingerprint{"cityview.draw", 172, &Fingerprint};
const DayHook kDrive{"cityview.draw", 172, &DriveCityDraw};

} // namespace
} // namespace opensc4::autotest
