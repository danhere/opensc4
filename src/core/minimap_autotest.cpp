// A test that gets the mini map's terrain picture drawn at every scale
// (docs/renderer.md, "The mini map"). The corner mini map and the Data Views
// window's picture control are each of one size, so a run reaches the row
// routines of two scales at most. Here a mini map of the test's own is made
// by the class factory for each of five sizes, started as the game starts
// one (its area set, then its Init) and brought up to date twice; the city's
// width against the picture's side gives the scale (+0x104, -2 to 2), which
// picks the row routine. Each is then painted twice on a canvas of its own.
// Nothing here compares pixels with the game's: that is the renderer's
// comparison (`renderer.minimap.draw` in compare mode), which sees every
// update and paint this test causes; of a paint only what it leaves in the
// canvas's own memory, which is why the token has the painted pixels too.
#include "core/autotest_hooks.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/log.h"

#include <opensc4/addresses.h>

#include <cISC4City.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace opensc4::autotest {
namespace {

// The mini map object (src/core/ui_map_6.cpp has the whole layout).
constexpr std::size_t kInterface = 0xD8;
constexpr std::size_t kSide = 0xE4;
constexpr std::size_t kBuffer = 0xF0;
constexpr std::size_t kStarted = 0xFC;
constexpr std::size_t kTerrainStale = 0xFD;
constexpr std::size_t kPictureStale = 0xFE;
constexpr std::size_t kShift = 0x104;
constexpr std::size_t kPixels = 0x114;
constexpr std::size_t kTiles = 0x120; // sixteen rows of bits: the 16 by 16 cell tiles to be redone
// The window base (src/core/window_paint_autotest.cpp).
constexpr std::size_t kCanvas = 0x68;
constexpr std::size_t kCanvasBuffer = 0x6C;
constexpr std::size_t kMarked = 0x70;
constexpr std::size_t kFlags = 0xC8;
constexpr uint32_t kOwnCanvas = 0x10000;

uint32_t Sum(const void* data, std::size_t size, uint32_t h = 2166136261u)
{
    const auto* const bytes = static_cast<const uint8_t*>(data);
    for (std::size_t i = 0; i < size; ++i)
        h = (h ^ bytes[i]) * 16777619u;
    return h;
}

// A picture buffer's memory as a checksum; 0 when it cannot be read.
uint32_t SumBuffer(void* buffer)
{
    if (!buffer)
        return 0;
    const uint8_t* const bits = Virtual<const uint8_t*>(buffer, 0x88);
    const uint32_t stride = Virtual<uint32_t>(buffer, 0x8C), height = Virtual<uint32_t>(buffer, 0x28);
    if (!bits || !stride || !height || height > 4096 || stride > 4096 * 4)
        return 0;
    return Sum(bits, std::size_t{stride} * height);
}

// The marked tiles that lie in the city: the terrain's update redoes those,
// with one call of a row routine each.
int MarkedTiles(char* map, int32_t width, int32_t depth)
{
    int marked = 0;
    for (int32_t z = 0; z < 16 && z * 16 < depth; ++z)
        for (int32_t x = 0; x < 32 && x * 16 < width; ++x)
            marked += (At<uint32_t>(map, kTiles + static_cast<std::size_t>(z) * 4) >> x) & 1;
    return marked;
}

// A window's place on its canvas, as the window base's paint driver sets it.
constexpr std::size_t kPlace = 0x34;

// What a paint left on the window's canvas, read through the lock of the
// canvas's buffer (+0x6C, slot 0xB8: the bits, the pitch and, fifth, the
// pixel format) as the mini map's paint writes it: the rows of the window's
// place as a checksum. The paint has a routine of its own (0x007A6FB0) for
// format 7, sixteen bits a pixel with four of alpha, which is what a
// window's own canvas is in a 16-bit video mode.
struct Painted {
    uint32_t format = 0xFF;
    uint32_t sum = 0;
};
Painted ReadCanvas(char* window)
{
    Painted p;
    void* const buffer = At<void*>(window, kCanvasBuffer);
    if (!buffer)
        return p;
    uint32_t info[8] = {};
    if (!Virtual<uint8_t>(buffer, 0xB8, &info[0], uint32_t{0})) {
        p.format = 0xFE;
        return p;
    }
    p.format = info[4];
    const std::size_t each = p.format >= 3 && p.format <= 7 ? 2 : p.format == 8 || p.format == 9 ? 4 : 0;
    const int32_t* const place = &At<int32_t>(window, kPlace);
    const uint8_t* const bits = reinterpret_cast<const uint8_t*>(info[0]);
    if (bits && each && place[0] >= 0 && place[1] >= 0 && place[2] > place[0] && place[3] > place[1] && place[3] <= 4096) {
        p.sum = 2166136261u;
        for (int32_t y = place[1]; y < place[3]; ++y)
            p.sum = Sum(bits + static_cast<std::size_t>(y) * info[1] + static_cast<std::size_t>(place[0]) * each,
                        static_cast<std::size_t>(place[2] - place[0]) * each, p.sum);
    }
    Virtual<void>(buffer, 0xBC);
    return p;
}

// The window's place on its canvas filled with one byte value, so that the
// next paint has every pixel to change.
void FillCanvas(char* window)
{
    void* const buffer = At<void*>(window, kCanvasBuffer);
    uint32_t info[8] = {};
    if (!buffer || !Virtual<uint8_t>(buffer, 0xB8, &info[0], uint32_t{0}))
        return;
    const std::size_t each = info[4] >= 3 && info[4] <= 7 ? 2 : info[4] == 8 || info[4] == 9 ? 4 : 0;
    const int32_t* const place = &At<int32_t>(window, kPlace);
    auto* const bits = reinterpret_cast<uint8_t*>(info[0]);
    if (bits && each && place[0] >= 0 && place[1] >= 0 && place[2] > place[0] && place[3] > place[1] && place[3] <= 4096)
        for (int32_t y = place[1]; y < place[3]; ++y)
            std::memset(bits + static_cast<std::size_t>(y) * info[1] + static_cast<std::size_t>(place[0]) * each, 0x55,
                        static_cast<std::size_t>(place[2] - place[0]) * each);
    Virtual<void>(buffer, 0xBC);
}

struct Totals {
    int made = 0;      // mini maps made and started
    int updates = 0;   // updates that had the terrain to redo
    int tiles = 0;     // tiles those updates redid: one call of a row routine each
    int paints = 0;    // paints called
    uint32_t scales = 0; // bit (scale + 2) for each scale an update redid tiles at
    uint32_t formats = 0; // bit n for each canvas format painted on
    uint32_t pixels = 2166136261u;
};

void DriveOne(Totals& t, int32_t side, int32_t width, int32_t depth)
{
    using ThisFn = void(__thiscall*)(void*);
    char* const face = static_cast<char*>(reinterpret_cast<void*(__cdecl*)()>(addr::SC4UI_F7A8AF0)());
    if (!face) {
        log::Write("autotest: mini map: side %d not made", side);
        return;
    }
    char* const map = face - kInterface;
    Virtual<uint32_t>(map, 0x4); // held by the test until the end
    Virtual<void>(map, 0xDC, int32_t{0}, int32_t{0}, side, side); // SetArea
    if (!Virtual<uint8_t>(map, 0x10)) {                           // Init: the buffer, the colours, the start
        log::Write("autotest: mini map: side %d: Init refused", side);
        Virtual<uint32_t>(map, 0x8);
        return;
    }
    At<uint32_t>(map, kFlags) |= 1; // on show, or nothing is drawn
    const bool onShow = reinterpret_cast<uint8_t(__cdecl*)(void*)>(addr::SC4UI_F7A6590)(map) != 0;
    if (!At<uint8_t>(map, kStarted) || !At<void*>(map, kPixels) || !onShow) {
        log::Write("autotest: mini map: side %d: started %d, pixels %p, on show %d", side, At<uint8_t>(map, kStarted),
                   At<void*>(map, kPixels), onShow);
    } else {
        ++t.made;
        const int32_t scale = At<int32_t>(map, kShift);
        const std::size_t bytes = std::size_t{At<uint32_t>(map, kSide)} * At<uint32_t>(map, kSide) * 4;
        // Twice: as started (every tile marked), and with every tile marked
        // again over the picture the first left.
        for (int pass = 0; pass < 2; ++pass) {
            if (pass)
                reinterpret_cast<ThisFn>(addr::SC4UI_F7A66C0)(map);
            const int before = MarkedTiles(map, width, depth);
            const bool stale = At<uint8_t>(map, kTerrainStale) && At<uint8_t>(map, kPictureStale);
            Virtual<void>(face, 0x24); // the picture brought up to date
            const int redone = before - MarkedTiles(map, width, depth);
            if (stale && redone > 0) {
                ++t.updates;
                t.tiles += redone;
                if (scale >= -2 && scale <= 2)
                    t.scales |= 1u << (scale + 2);
            }
            const uint32_t own = At<void*>(map, kPixels) ? Sum(At<void*>(map, kPixels), bytes) : 0;
            const uint32_t buffer = SumBuffer(At<void*>(map, kBuffer));
            t.pixels = (t.pixels ^ own) * 16777619u;
            t.pixels = (t.pixels ^ buffer) * 16777619u;
            log::Write("autotest: mini map: side %u, scale %d, update %d: %d tiles marked, %d redone, pixels %08X, buffer %08X",
                       At<uint32_t>(map, kSide), scale, pass + 1, before, redone, own, buffer);
        }

        // The paint, on a canvas of the window's own (the screen's is not a
        // picture in memory), through the window base's driver.
        Virtual<void>(map, 0x110, kOwnCanvas, uint32_t{1});
        if (!At<void*>(map, kCanvas))
            Virtual<void>(map, 0x17C);
        if (At<void*>(map, kCanvas)) {
            // Twice: the first paint on a new canvas (which the renderer's
            // comparison cannot repeat, the canvas's memory being new), then
            // one over the canvas filled with another colour.
            for (int pass = 0; pass < 2; ++pass) {
                if (pass)
                    FillCanvas(map);
                At<uint32_t>(map, kFlags) |= 1;
                At<uint8_t>(map, kMarked) = 1;
                const uint8_t answer = Virtual<uint8_t>(map, 0x1EC);
                const Painted painted = ReadCanvas(map);
                ++t.paints;
                if (painted.format < 32)
                    t.formats |= 1u << painted.format;
                t.pixels = (t.pixels ^ painted.sum) * 16777619u;
                log::Write("autotest: mini map: side %u, paint %d (%d) on a canvas of format %u: %08X", At<uint32_t>(map, kSide),
                           pass + 1, answer, painted.format, painted.sum);
            }
        } else {
            log::Write("autotest: mini map: side %u got no canvas", At<uint32_t>(map, kSide));
        }
        Virtual<void>(map, 0x110, kOwnCanvas, uint32_t{0});
    }
    reinterpret_cast<ThisFn>(addr::SC4UI_F7A77E0)(map); // out of use: the ticks and the pixels dropped
    Virtual<uint8_t>(map, 0x14);                        // Shutdown: the messages dropped
    if (const uint32_t held = Virtual<uint32_t>(map, 0x8)) // let go: deleted
        log::Write("autotest: mini map: side %d is still held %u times", side, held);
}

std::string g_token;

// OPENSC4_TEST_MINI_MAP=<day>: on that run day a mini map of each of five
// sizes (sides 32 to 512) is made, started, brought up to date twice and
// painted, then put away. The token says how many were made, how many
// updates redid the terrain and how many tiles they redid (one call of a row
// routine each), the scales that were drawn (bit 0 for -2 to bit 4 for 2),
// how many paints were called and on which canvas formats (a bit each), and
// a checksum of every picture.
void DriveMiniMap(const Day& day)
{
    if (!OnRunDay(day, "OPENSC4_TEST_MINI_MAP") || !game::IsBuild641())
        return;
    Totals t;
    void* const city = Global<void*>(addr::SC4_City);
    const int32_t width = city ? Virtual<int32_t>(city, 0x174) : 0, depth = city ? Virtual<int32_t>(city, 0x178) : 0;
    for (const int32_t side : {32, 64, 128, 256, 512})
        DriveOne(t, side, width, depth);
    char text[160];
    std::snprintf(text, sizeof text, " | minimap %d updates %d tiles %d scales %02X paints %d formats %X pixels %08X", t.made,
                  t.updates, t.tiles, t.scales, t.paints, t.formats, t.pixels);
    g_token = text;
    log::Write("autotest: mini map driven (city %d by %d cells):%s", width, depth, text);
}

void Fingerprint(cISC4City*, std::string& line)
{
    line += g_token;
}

const FingerprintPart kFingerprint{"minimap", 171, &Fingerprint};
const DayHook kDrive{"minimap", 171, &DriveMiniMap};

} // namespace
} // namespace opensc4::autotest
