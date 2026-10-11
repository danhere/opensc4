// game.bulldoze: three more ways to use the bulldozer, chosen by the keys
// held while it works (docs/bulldoze.md). An improvement beyond the original
// and part of the game as it comes: with no key held the bulldozer is the
// original's, so there is no setting to turn it on.
//
//   Ctrl          only trees and other flora
//   Shift         only roads, rails and the other transport networks
//   Ctrl + Shift  everything but the transport networks
//   Alt           a diagonal strip in place of the rectangle (with any of
//                 the above)
//
// The demolishing is the game's own (cSC4Demolition::DemolishRegion): it
// takes an occupant filter, which the bulldoze tool leaves empty, and this
// passes one for the tool's calls. The idea of flora-only and network-only
// bulldozing is from the Bulldoze Extensions plugin by Nicholas Hayes; the
// code and the way of choosing a mode are ours.
#include "core/game_bulldoze.h"

#include "core/address_check.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"

#include <cIGZUnknown.h>
#include <cISC4NetworkOccupant.h>
#include <cISC4Occupant.h>
#include <cISC4OccupantFilter.h>
#include <cRZBaseString.h>

#include <windows.h>

#include <cstdint>
#include <cstdlib>
#include <string>

#include <opensc4/addresses.h>

namespace opensc4::game_bulldoze {
namespace {

enum class Mode { All, Flora, Networks, NotNetworks };

constexpr uint32_t kTypeFlora = 0x74758926;

// The bulldoze tool's code (cSC4ViewInputControlDemolish, vtable 0x00A901A0):
// only its calls of DemolishRegion get a filter.
constexpr std::uintptr_t kToolBegin = 0x004B8830, kToolEnd = 0x004B9E20;

int g_testMode = -1; // OPENSC4_TEST_BULLDOZE_MODE: a mode for every caller
bool g_testDiagonal = false; // OPENSC4_TEST_BULLDOZE_DIAGONAL=1: a strip from the region's corners

// The tool whose preview ran last: its drag runs from the cell at +0x58
// (x), +0x5C (z) to the cell at +0x60, +0x64.
char* g_tool = nullptr;

Mode Current()
{
    if (g_testMode >= 0)
        return static_cast<Mode>(g_testMode);
    const bool control = GetKeyState(VK_CONTROL) < 0, shift = GetKeyState(VK_SHIFT) < 0;
    return control ? (shift ? Mode::NotNetworks : Mode::Flora) : (shift ? Mode::Networks : Mode::All);
}

// A road, rail or other transport piece; not a water pipe or a power line.
bool IsTransport(cISC4Occupant* occupant)
{
    cISC4NetworkOccupant* piece = nullptr;
    if (!occupant->QueryInterface(GZIID_cISC4NetworkOccupant, reinterpret_cast<void**>(&piece)) || !piece)
        return false;
    const bool transport = !piece->IsOfType(cISC4NetworkOccupant::WaterPipe) &&
                           !piece->IsOfType(cISC4NetworkOccupant::PowerPole);
    piece->Release();
    return transport;
}

bool Includes(Mode mode, cISC4Occupant* occupant)
{
    switch (mode) {
    case Mode::Flora: return static_cast<uint32_t>(occupant->GetType()) == kTypeFlora;
    case Mode::Networks: return IsTransport(occupant);
    case Mode::NotNetworks: return !IsTransport(occupant);
    default: return true;
    }
}

class Filter final : public cISC4OccupantFilter {
public:
    bool QueryInterface(uint32_t riid, void** object) override
    {
        if (riid == GZIID_cIGZUnknown || riid == GZIID_cISC4OccupantFilter) {
            *object = this;
            return true;
        }
        *object = nullptr;
        return false;
    }
    // Static: lives for the whole process.
    uint32_t AddRef() override { return 1; }
    uint32_t Release() override { return 1; }

    bool IsOccupantIncluded(cISC4Occupant* occupant) override { return occupant && Includes(mode, occupant); }
    bool IsOccupantTypeIncluded(uint32_t type) override { return mode != Mode::Flora || type == kTypeFlora; }
    bool IsPropertyHolderIncluded(cISCPropertyHolder*) override { return true; }

    Mode mode = Mode::All;
};

Filter g_filter;

// Clears from the region (SC4CellRegion: the rectangle, then at +0x10 the
// cells: width +0x14, depth +0x18, a row of bits per x at +0x20) every cell
// off the diagonal strip of a drag from (sx, sz) to (ex, ez). The strip runs
// from the start cell at 45 degrees towards the end cell; a drag that is
// longer one way than the other widens it by the difference, so a square
// drag is a line one cell wide. Returns the cells left.
int32_t KeepDiagonal(int32_t* region, int32_t sx, int32_t sz, int32_t ex, int32_t ez)
{
    const uint32_t width = static_cast<uint32_t>(region[5]), depth = static_cast<uint32_t>(region[6]);
    uint32_t** const rows = reinterpret_cast<uint32_t**>(region[8]);
    if (!rows)
        return 0;
    const int32_t dx = ex >= sx ? 1 : -1, dz = ez >= sz ? 1 : -1;
    // How far the drag is longer along x (positive) or along z (negative).
    const int32_t extra = (ex - sx) * dx - (ez - sz) * dz;
    int32_t left = 0;
    for (int32_t x = region[0]; x <= region[2]; ++x)
        for (int32_t z = region[1]; z <= region[3]; ++z) {
            const uint32_t i = static_cast<uint32_t>(x - region[0]), j = static_cast<uint32_t>(z - region[1]);
            if (i >= width || j >= depth || !(rows[i][j >> 5] & (1u << (j & 31))))
                continue;
            // The cell's offset from the line through the start cell.
            const int32_t off = (x - sx) * dx - (z - sz) * dz;
            if (extra >= 0 ? (off >= 0 && off <= extra) : (off <= 0 && off >= extra))
                ++left;
            else
                rows[i][j >> 5] &= ~(1u << (j & 31));
        }
    return left;
}

using PreviewFn = uint32_t(__fastcall*)(char* self, void* edx);
PreviewFn g_preview = nullptr;

// The tool's preview (0x004B93B0), which every DemolishRegion of the tool
// follows: only remembers the tool.
uint32_t __fastcall Preview(char* self, void* edx)
{
    g_tool = self;
    return g_preview(self, edx);
}

using MoveFn = uint8_t(__fastcall*)(char* self, void* edx, int32_t x, int32_t z, uint32_t modifiers);
MoveFn g_move = nullptr;

// The tool's mouse move (0x004B9B70, slot 20), hovering or dragging: after
// it, the label by the cursor says what the held keys give. The view is the
// tool's +0x1C.
uint8_t __fastcall MouseMove(char* self, void* edx, int32_t x, int32_t z, uint32_t modifiers)
{
    const uint8_t taken = g_move(self, edx, x, z, modifiers);
    if (void* const view = At<void*>(self, 0x1C))
        SetLabel(view, static_cast<int>(Current()), GetKeyState(VK_MENU) < 0);
    return taken;
}

using EndFn = void(__fastcall*)(char* self, void* edx);
EndFn g_end = nullptr;

// The tool's end of a drag (0x004B8ED0): on mouse up, on Escape and when the
// tool is put away. The label goes with it.
void __fastcall EndDrag(char* self, void* edx)
{
    if (void* const view = At<void*>(self, 0x1C))
        ClearLabel(view);
    g_end(self, edx);
}

using RegionFn = uint8_t(__fastcall*)(void* self, void* edx, uint8_t demolish, void* region, int32_t privilege,
                                      uint32_t flags, uint8_t clearZoned, cISC4OccupantFilter* filter, int64_t* cost,
                                      void* demolished, void* occupant, int32_t a, int32_t b);
RegionFn g_region = nullptr;

uint8_t __fastcall DemolishRegion(void* self, void* edx, uint8_t demolish, void* region, int32_t privilege,
                                  uint32_t flags, uint8_t clearZoned, cISC4OccupantFilter* filter, int64_t* cost,
                                  void* demolished, void* occupant, int32_t a, int32_t b)
{
    const auto caller = reinterpret_cast<std::uintptr_t>(__builtin_return_address(0));
    const bool tool = caller >= kToolBegin && caller < kToolEnd;
    if (!filter && (g_testMode >= 0 || tool)) {
        const Mode mode = Current();
        if (mode != Mode::All) {
            g_filter.mode = mode;
            filter = &g_filter;
        }
    }
    int32_t* const cells = static_cast<int32_t*>(region);
    if (g_testDiagonal && cells && privilege == 1 && flags == 1) {
        const int32_t left = KeepDiagonal(cells, cells[0], cells[1], cells[2], cells[3]);
        log::Write("bulldoze: diagonal, %d cells", left);
    } else if (tool && cells && g_tool && GetKeyState(VK_MENU) < 0) {
        KeepDiagonal(cells, At<int32_t>(g_tool, 0x58), At<int32_t>(g_tool, 0x5C), At<int32_t>(g_tool, 0x60),
                     At<int32_t>(g_tool, 0x64));
    }
    return g_region(self, edx, demolish, region, privilege, flags, clearZoned, filter, cost, demolished, occupant, a, b);
}

using ShownFn = uint8_t(__fastcall*)(void* self, void* edx, cISC4Occupant* occupant);
ShownFn g_shown = nullptr;

uint8_t __fastcall IsOccupantShown(void* self, void* edx, cISC4Occupant* occupant)
{
    if (!g_shown(self, edx, occupant))
        return 0;
    const Mode mode = Current();
    return mode == Mode::All || Includes(mode, occupant) ? 1 : 0;
}

bool Install(registry::Mode)
{
    char value[8];
    if (const DWORD n = GetEnvironmentVariableA("OPENSC4_TEST_BULLDOZE_MODE", value, sizeof(value));
        n > 0 && n < sizeof(value))
        g_testMode = std::atoi(value) & 3;
    g_testDiagonal = GetEnvironmentVariableA("OPENSC4_TEST_BULLDOZE_DIAGONAL", value, sizeof(value)) == 1 &&
                     value[0] == '1';
    const addr::Entry* region = addr_check::Find(addr::cSC4Demolition_DemolishRegion);
    const addr::Entry* shown = addr_check::Find(addr::cSC4ViewInputControlDemolish_IsOccupantShown);
    const addr::Entry* preview = addr_check::Find(addr::cSC4ViewInputControlDemolish_Preview);
    const addr::Entry* move = addr_check::Find(addr::cSC4ViewInputControlDemolish_OnMouseMove);
    const addr::Entry* end = addr_check::Find(addr::cSC4ViewInputControlDemolish_EndDrag);
    const bool ok = region && shown && preview && hook::Install(*region, &DemolishRegion, &g_region) &&
                    hook::Install(*shown, &IsOccupantShown, &g_shown) &&
                    hook::Install(*preview, &Preview, &g_preview) && move && end &&
                    hook::Install(*move, &MouseMove, &g_move) && hook::Install(*end, &EndDrag, &g_end);
    log::Write("bulldoze: Ctrl flora only, Shift networks only, Ctrl+Shift all but networks, Alt diagonal: %s",
               ok ? "installed" : "FAILED");
    return ok;
}

const bool g_registered = registry::Register({
    "game.bulldoze",
    "game",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4Demolition_DemolishRegion,
    "Bulldozer modes by held keys: flora only, transport networks only, all but networks",
    false,
    Install,
    true, // nothing changes until a key is held
});

} // namespace

namespace {
// The cursor text's ID in the view's list (ours) and its priority among the
// texts shown together.
constexpr uint32_t kLabelId = 0x4F534342; // "OSCB"
constexpr uint32_t kLabelPriority = 5;
} // namespace

void SetLabel(void* view, int mode, bool diagonal)
{
    static const char* const kWhat[4] = {"", "Trees only", "Roads and rails only", "Everything but roads and rails"};
    std::string text = kWhat[mode & 3];
    if (diagonal)
        text += text.empty() ? "Diagonal" : ", diagonal";
    if (text.empty()) {
        ClearLabel(view);
        return;
    }
    // SetCursorText (slot 40): (id, priority, title, body, icon). The body is
    // given, empty: without one the game empties the title of a text it
    // already has.
    cRZBaseString title(text.c_str()), body;
    Virtual<void>(view, 0xA0, kLabelId, kLabelPriority, static_cast<cIGZString*>(&title),
                  static_cast<cIGZString*>(&body), 0);
}

void ClearLabel(void* view)
{
    Virtual<void>(view, 0xA4, kLabelId); // ClearCursorText (slot 41)
}

} // namespace opensc4::game_bulldoze
