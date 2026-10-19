// The zoning tool's and the cheat tool's pointer drawing (T10): the last game
// code of both classes and the model holder's placing. Replacement, off by
// default:
//
//   ui.tools.zone2: the zoning tool's OnMouseMove (0x004DBE40, slot 20 of
//     0x00A92CA8), the cheat tool's FindFootprint (0x004D9EE0) and
//     OnMouseMove (0x004DA1B0, slot 20 of 0x00A92AC0), and the base class's
//     model holder Place (0x005FB360).
//
// With ui.tools.zone (tool_zone.cpp) every function of both classes is ours.
//
// Place snaps a point to its cell and gives the held model that place, the
// identity rotation and the cell size times 16 as its scale. Its callers are
// the lot tool's preview update and the OnMouseMove functions of the zoning
// tool and the two network tools; all of them call it by address, keep
// nothing in EAX, ECX or EDX across it and read nothing it leaves on the
// stack, so a detour at its entry serves them all.
//
// The zoning tool's OnMouseMove moves the pointer model and the outline of
// the cell under the pointer, and while dragging moves the drag's end. The
// cheat tool (the cheat 0x0BFD8064) shows a footprint model over the network
// piece under the pointer and, on a straight piece of a road, rail, street,
// one-way road, avenue or highway, a prop of the lot configuration that would
// stand there.
//
// Floats: the game runs the x87 at 53 bit (docs/pollution.md, "Floating
// point") and keeps its chains of operations in registers, so ours computes
// them in double and rounds to float only where the game stores a float. The
// floors are the game's floor (0x009EFF60), called at its address.
//
// Every function was written from the disassembly. Calls into the game stay
// calls by address or by vtable slot; the three functions call each other by
// address too, so each reaches ours only through its own hook.
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace opensc4::tool_zone_2 {
namespace {

// The zoning tool's fields used here (tool_zone.cpp has all of them).
namespace zone {
constexpr std::size_t kView = 0x1C;      // the city view (base class)
constexpr std::size_t kOverlay = 0x2C;   // the terrain's cell overlay
constexpr std::size_t kDragging = 0x40;  // byte
constexpr std::size_t kCurX = 0x4C;      // the cell under the pointer (x, z)
constexpr std::size_t kCell = 0x6C;      // float, the terrain's slot 0x18
constexpr std::size_t kPointer = 0x90;   // the pointer model's holder
constexpr std::size_t kOutline = 0x94;   // the outline model
} // namespace zone

// The cheat tool's fields (0x38 bytes; up to +0x24 the base class).
namespace cheat {
constexpr std::size_t kProp = 0x28;   // the prop of the lot configuration, or null
constexpr std::size_t kModel = 0x2C;  // the footprint model, or null
constexpr std::size_t kKind = 0x30;   // the kind the model was made for (Init sets 1)
constexpr std::size_t kConfig = 0x34; // the lot configuration of the prop, or 0
} // namespace cheat

// The modifier bit OnMouseMove takes for the left button held.
constexpr uint32_t kLeftButton = 0x10;

// The network types FindFootprint asks for, in its order.
constexpr int32_t kFootprintTypes[] = {0, 2, 3, 6, 10, 12};
constexpr uint32_t kFootprintMask = 0x1F4F;
constexpr uint32_t kNetworkOccupants = 0x144D;
constexpr float kHalfStep = 8.0f;

// The two properties the cheat tool's prop gets.
constexpr uint32_t kPropertyA = 0x029244DB;
constexpr uint32_t kPropertyB = 0x29F013E6;

// What a model's slot 8 (SetTransform) and slot 15 (GetTransform) take: a
// count and flags, a 3x3 rotation, the place and the scale. The two bytes
// after the flags are what the game's stack held; ours are zero.
struct Transform {
    uint8_t count;
    uint8_t flags;
    uint8_t unused[2];
    float rotation[9];
    float place[3];
    float scale;
};
static_assert(sizeof(Transform) == 0x38);

constexpr uint8_t kPlaced = 0x1;
constexpr uint8_t kScaled = 0x2;

Transform Identity()
{
    Transform t{};
    t.rotation[0] = 1.0f;
    t.rotation[4] = 1.0f;
    t.rotation[8] = 1.0f;
    t.scale = 1.0f;
    return t;
}

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

void Release(void* object)
{
    Virtual<uint32_t>(object, 0x8);
}

double Floor(double value)
{
    return Cdecl<double>(addr::CRT_floor, value);
}

// The game's _ftol2 (0x009EEF04): truncation toward zero to 64 bits, the low
// 32 kept; what does not fit in 64 bits (a NaN among them) is the x87
// "integer indefinite", whose low word is 0.
int32_t Truncate(double value)
{
    if (!(value > -9223372036854775808.0 && value < 9223372036854775808.0))
        return 0;
    return static_cast<int32_t>(static_cast<uint32_t>(static_cast<uint64_t>(static_cast<int64_t>(value))));
}

// The three floats a pick is handed, set from the game's unset value (copied
// as bits, as the game's MOVs do).
void Unset(float* point, std::uintptr_t unset)
{
    for (int i = 0; i < 3; ++i)
        std::memcpy(&point[i], reinterpret_cast<const void*>(unset), sizeof(float));
}

// The cell centre of one coordinate as the game computes it: the quotient,
// its floor, times the cell, plus `half` (which differs between the
// callers), in double; the game stores the sum to a float.
float Snap(double value, double cell, double half)
{
    const double corner = Floor(value / cell) * cell;
    return static_cast<float>(corner + half);
}

// The three flags the keys and the pointer hand the display service (slot 7,
// Service43DB0), as tool_zone.cpp gives them.
void SetModifiers(uint32_t modifiers)
{
    const uint32_t third = (modifiers >> 1) & 0xFFFFFF01u;
    const uint32_t second = (~(modifiers & 0xFFu) & 0xFFu) & 0xFFFFFF01u;
    const uint32_t first = (modifiers >> 2) & 0xFFFFFF01u;
    Virtual<void>(Global<void*>(addr::SC4_Service43DB0), 0x1C, first, second, third);
}

// A lot configuration's size in cells times 8 (half its size in world units),
// taken from the corner as the game does: the size is shifted left 4 and
// right 1 as an unsigned word (so it is below 2^31 and the game's "add 2^32
// when negative" never applies), loaded exactly, subtracted in double and
// stored to a float.
float LessHalfSize(float corner, uint32_t size)
{
    const uint32_t half = (size << 4) >> 1;
    return static_cast<float>(static_cast<double>(corner) - static_cast<double>(half));
}

// --- The model holder ------------------------------------------------------------------

// The game's 0x005FB360 (const float point[3]), on a holder: the held model's
// transform from the point. The terrain's cell size is asked five times, as
// the game asks it.
void __fastcall Place(void** holder, void*, const float* point)
{
    if (!*holder)
        return;
    void* const terrain = Global<void*>(addr::SC4_Terrain);
    Transform t = Identity();
    // The cell size comes back on the x87 stack; the game keeps it as a
    // double.
    const float x = point[0];
    const double cellX = Virtual<double>(terrain, 0x18);
    const double cornerX = Floor(static_cast<double>(x) / cellX) * cellX;
    const float placeX = static_cast<float>(Virtual<double>(terrain, 0x18) * 0.0625 + cornerX);
    float y;
    std::memcpy(&y, &point[1], sizeof y);
    const float z = point[2];
    const double cellZ = Virtual<double>(terrain, 0x18);
    const double cornerZ = Floor(static_cast<double>(z) / cellZ) * cellZ;
    const float placeZ = static_cast<float>(Virtual<double>(terrain, 0x18) * 0.0625 + cornerZ);
    const double scale = Virtual<double>(terrain, 0x18) * 16.0;
    t.scale = static_cast<float>(scale);
    t.flags = static_cast<uint8_t>((scale != 1.0 ? kScaled : 0) | kPlaced);
    t.place[0] = placeX;
    t.place[1] = y;
    t.place[2] = placeZ;
    t.count = 2;
    Virtual<void>(*holder, 0x20, &t);
}

// --- The zoning tool -------------------------------------------------------------------

// The game's 0x004DBE40 (slot 20, OnMouseMove; x, z, modifiers).
uint8_t __fastcall ZoneOnMouseMove(char* self, void*, int32_t x, int32_t z, uint32_t modifiers)
{
    SetModifiers(modifiers);
    float point[3];
    Unset(point, addr::SC4ViewTool_UnsetPoint);
    if (!Virtual<uint8_t>(At<void*>(self, zone::kView), 0x90, x, z, point, uint32_t{0})) {
        This<void>(addr::SC4ViewModel_Show, self + zone::kPointer, uint32_t{0});
        return 0;
    }
    This<void>(addr::SC4ViewModel_Place, self + zone::kPointer, static_cast<const float*>(point));
    const bool overlayBusy = Virtual<uint8_t>(At<void*>(self, zone::kOverlay), 0x50) != 0;
    const bool showPointer = !overlayBusy && !At<uint8_t>(self, zone::kDragging);
    This<void>(addr::SC4ViewModel_Show, self + zone::kPointer, uint32_t{showPointer ? 1u : 0u});

    if (At<void*>(self, zone::kOutline)) {
        const float cell = At<float>(self, zone::kCell);
        Transform t = Identity();
        const float placeX = Snap(point[0], cell, static_cast<double>(cell) * 0.5);
        const float placeZ = Snap(point[2], cell, static_cast<double>(cell) * 0.5);
        t.scale = cell;
        t.flags = static_cast<uint8_t>((cell != 1.0f ? kScaled : 0) | kPlaced);
        t.place[0] = placeX;
        t.place[1] = point[1];
        t.place[2] = placeZ;
        t.count = 2;
        Virtual<void>(At<void*>(self, zone::kOutline), 0x20, &t);
        if (!At<uint8_t>(self, zone::kDragging)) {
            if (!Virtual<uint8_t>(At<void*>(self, zone::kOutline), 0x14))
                Virtual<void>(At<void*>(self, zone::kOutline), 0xC, uint32_t{0});
        } else if (Virtual<uint8_t>(At<void*>(self, zone::kOutline), 0x14)) {
            Virtual<void>(At<void*>(self, zone::kOutline), 0x10, uint32_t{0});
        }
    }

    if (!At<uint8_t>(self, zone::kDragging))
        return 0;
    if (!(modifiers & kLeftButton)) {
        Virtual<void>(self, 0x6C); // CancelDrag
        return 1;
    }
    const double inverse = 1.0 / static_cast<double>(At<float>(self, zone::kCell));
    int32_t cell[2];
    cell[0] = Truncate(static_cast<double>(point[0]) * inverse);
    cell[1] = Truncate(static_cast<double>(point[2]) * inverse);
    int32_t* const current = &At<int32_t>(self, zone::kCurX);
    if (Cdecl<uint8_t>(addr::SC4_CellPairsDiffer, cell, current)) {
        current[0] = cell[0];
        current[1] = cell[1];
        This<void>(addr::cSC4ViewInputControlZone_UpdateHover, self);
        This<uint8_t>(addr::cSC4ViewInputControlZone_UpdatePreview, self);
    }
    return 1;
}

// --- The cheat tool --------------------------------------------------------------------

// The game's 0x004D9EE0 (cell x, z, int32* kind, uint32* lot configuration,
// int32* orientation, float point[3]); `this` unused. See the address entry.
void __fastcall FindFootprint(void*, void*, int32_t x, int32_t z, int32_t* kind, uint32_t* config,
                              int32_t* orientation, float* point)
{
    *kind = 0;
    *config = 0;
    *orientation = 0;
    Virtual<void>(Global<void*>(addr::SC4_City), 0x184, x, z, &point[0], &point[2]);
    void* const piece = Virtual<void*>(Global<void*>(addr::SC4_Service43D54), 0x20, x, z, kNetworkOccupants,
                                       uint32_t{1});
    if (!piece)
        return;
    int32_t type = -1;
    for (const int32_t candidate : kFootprintTypes) {
        if (Virtual<uint8_t>(piece, 0x58, candidate)) {
            type = candidate;
            break;
        }
    }
    if (type < 0)
        return;
    const uint32_t mask = Virtual<uint32_t>(piece, 0x30) & kFootprintMask;
    if (mask != ((1u << type) & mask))
        return;
    uint32_t sides = Virtual<uint32_t>(piece, 0x98, type);
    bool joined[4];
    bool flipped[4];
    int count = 0;
    for (int i = 0; i < 4; ++i) {
        const uint32_t edge = sides & 3;
        if (edge == 1 || edge == 3)
            return;
        joined[i] = edge == 2;
        if (joined[i])
            ++count;
        flipped[i] = ((sides >> 2) & 1) != 0;
        sides >>= 8;
    }
    if (count != 2)
        return;
    const uint32_t id = reinterpret_cast<const uint32_t*>(addr::SC4CheatTool_FootprintIds)[type];
    switch (type) {
    case 0:
    case 3:
    case 10:
        if (joined[0] && joined[2]) {
            *kind = 1;
            *orientation = 2;
            *config = id;
        } else if (joined[1] && joined[3]) {
            *kind = 1;
            *orientation = 1;
            *config = id;
        }
        return;
    case 2:
    case 6:
    case 12:
        if (joined[0] && joined[2]) {
            *kind = 2;
            *orientation = 2;
            point[2] = flipped[1] ? point[2] - kHalfStep : point[2] + kHalfStep;
            *config = id;
        } else if (joined[1] && joined[3]) {
            *kind = 2;
            *orientation = 1;
            point[0] = flipped[0] ? point[0] - kHalfStep : point[0] + kHalfStep;
            *config = id;
        }
        return;
    default:
        return;
    }
}

// The footprint model for a new kind: the old one hidden and let go, the new
// one asked from the model service by name (kinds 0, 1 and 2; any other kind
// leaves none).
void ReplaceModel(char* self, int32_t kind)
{
    void** const holder = &At<void*>(self, cheat::kModel);
    if (*holder) {
        if (Virtual<uint8_t>(*holder, 0x14))
            Virtual<void>(*holder, 0x10, uint32_t{0});
        if (void* const old = *holder) {
            *holder = nullptr;
            Release(old);
        }
    }
    At<int32_t>(self, cheat::kKind) = kind;
    std::uintptr_t name = 0;
    if (kind == 0)
        name = addr::SC4CheatTool_FootprintNotOk;
    else if (kind == 1 || kind == 2)
        name = addr::SC4CheatTool_FootprintOk;
    if (!name)
        return;
    void* const models = Global<void*>(addr::SC4_Service43D1C);
    if (void* const old = *holder) {
        *holder = nullptr;
        Release(old);
    }
    Virtual<void>(models, 0x1C, reinterpret_cast<const char*>(name), holder);
}

// The prop for a new lot configuration: the old prop taken out of the prop
// manager and let go; a new one made at the configuration's corner when the
// configuration and its prop type are known. `width` and `depth` start as
// the game's stack slots held them (the configuration and the kind), in case
// the size query leaves them.
void ReplaceProp(char* self, uint32_t config, int32_t kind)
{
    At<uint32_t>(self, cheat::kConfig) = config;
    void** const holder = &At<void*>(self, cheat::kProp);
    if (*holder) {
        void* const props = Global<void*>(addr::cSC4PropManager_Global);
        Virtual<void>(props, 0x74, Virtual<void*>(*holder, 0xC));
        if (void* const old = *holder) {
            *holder = nullptr;
            Release(old);
        }
    }
    if (!At<uint32_t>(self, cheat::kConfig))
        return;
    void* const configuration = Virtual<void*>(Global<void*>(addr::SC4_Service43D18), 0x20,
                                               At<uint32_t>(self, cheat::kConfig));
    if (!configuration)
        return;
    uint32_t width = config;
    uint32_t depth = static_cast<uint32_t>(kind);
    Virtual<void>(configuration, 0x1C, &width, &depth);
    float type[3] = {};
    const char* const info = Virtual<const char*>(configuration, 0x60);
    const uint32_t propType = **reinterpret_cast<uint32_t* const*>(info + 0x28);
    float corner[3];
    std::memcpy(&corner[0], info + 0x4, sizeof(float));
    std::memcpy(&corner[1], info + 0x8, sizeof(float));
    const float cornerZ = *reinterpret_cast<const float*>(info + 0xC);
    corner[0] = LessHalfSize(corner[0], width);
    corner[2] = LessHalfSize(cornerZ, depth);
    if (!Virtual<uint8_t>(Global<void*>(addr::SC4_Service43D68), 0x38, propType, type))
        return;
    void* const props = Global<void*>(addr::cSC4PropManager_Global);
    void* const out = This<void*>(addr::SC4RefPtr_Reset, holder);
    if (!Virtual<uint8_t>(props, 0x40, type, out))
        return;
    void* const prop = Virtual<void*>(*holder, 0xC);
    Virtual<void>(prop, 0x74, uint32_t{0x10});
    Cdecl<uint8_t>(addr::SC4_SetPropertyBool, Virtual<void*>(prop, 0x18), kPropertyA, uint32_t{1});
    Cdecl<uint8_t>(addr::SC4_SetPropertyBool, Virtual<void*>(prop, 0x18), kPropertyB, uint32_t{1});
    Virtual<void>(prop, 0x24, corner);
}

// The game's 0x004DA1B0 (slot 20, OnMouseMove; x, z, modifiers unused).
uint8_t __fastcall CheatOnMouseMove(char* self, void*, int32_t x, int32_t z, uint32_t)
{
    float pick[3];
    Unset(pick, addr::SC4CheatTool_UnsetPoint);
    if (!Virtual<uint8_t>(Global<void*>(addr::SC4_Service43DC4), 0x90, x, z, pick, uint32_t{0}))
        return 1;
    void* const city = Global<void*>(addr::SC4_City);
    int32_t cellX = 0;
    int32_t cellZ = 0;
    Virtual<void>(city, 0x17C, pick[0], pick[2], &cellX, &cellZ);
    Virtual<void>(Global<void*>(addr::SC4_City), 0x184, cellX, cellZ, &pick[0], &pick[2]);
    float point[3];
    Unset(point, addr::SC4CheatTool_UnsetPoint);
    int32_t kind = 0;
    uint32_t config = 0;
    int32_t orientation = 0;
    This<void>(addr::SC4CheatTool_FindFootprint, self, cellX, cellZ, &kind, &config, &orientation, point);
    point[1] = Virtual<float>(Global<void*>(addr::SC4_Terrain), 0x34, point[0], point[2]);

    if (At<int32_t>(self, cheat::kKind) != kind)
        ReplaceModel(self, kind);
    if (At<void*>(self, cheat::kModel)) {
        if (!Virtual<uint8_t>(At<void*>(self, cheat::kModel), 0x14))
            Virtual<void>(At<void*>(self, cheat::kModel), 0xC, uint32_t{0});
        Transform t = Identity();
        Virtual<void>(At<void*>(self, cheat::kModel), 0x3C, &t);
        t.place[0] = point[0];
        t.place[1] = point[1];
        t.place[2] = point[2];
        t.flags |= kPlaced;
        t.count = static_cast<uint8_t>(t.count + 1);
        Virtual<void>(At<void*>(self, cheat::kModel), 0x20, &t);
    }
    if (config != At<uint32_t>(self, cheat::kConfig))
        ReplaceProp(self, config, kind);
    if (At<void*>(self, cheat::kProp)) {
        Virtual<void>(Virtual<void*>(At<void*>(self, cheat::kProp), 0xC), 0x24, point);
        Virtual<void>(At<void*>(self, cheat::kProp), 0x28, orientation);
    }
    return 1;
}

bool InstallZone2(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[4] = {};
    static const struct {
        std::uintptr_t address;
        void* ours;
    } hooks[] = {
        {addr::SC4ViewModel_Place, reinterpret_cast<void*>(&Place)},
        {addr::cSC4ViewInputControlZone_OnMouseMove, reinterpret_cast<void*>(&ZoneOnMouseMove)},
        {addr::SC4CheatTool_FindFootprint, reinterpret_cast<void*>(&FindFootprint)},
        {addr::SC4CheatTool_OnMouseMove, reinterpret_cast<void*>(&CheatOnMouseMove)},
    };
    constexpr std::size_t count = sizeof(hooks) / sizeof(hooks[0]);
    static_assert(count <= sizeof(originals) / sizeof(originals[0]));
    for (std::size_t i = 0; i < count; ++i) {
        const addr::Entry* found = addr_check::Find(hooks[i].address);
        if (!found || !hook::Install(*found, hooks[i].ours, &originals[i]))
            return false;
    }
    return true;
}

const bool g_registered = registry::Register({
    "ui.tools.zone2",
    "ui",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4ViewInputControlZone_OnMouseMove,
    "Our zoning tool's and cheat tool's pointer drawing and the model holder's Place (docs/tools.md)",
    false,
    InstallZone2,
});

} // namespace
} // namespace opensc4::tool_zone_2
