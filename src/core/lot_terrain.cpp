// Our cSC4LotDeveloper, the terrain part (build 641; code 0x006C4EF0 to
// 0x006C7280): where a lot configuration object stands, the altitude, range,
// slope and flatness of an area, the foundation by configuration, the
// development cost, lot props, the travel populations of a business unit and
// the boundary textures. Twenty slots of the primary vtable (0x00AB1B98) and
// four helpers the build part calls directly (detours). Group
// simulators.lot.terrain.
//
// The other functions with their entry in the range stay the game's: Write
// (0x006C66F0, the object part's) and the container code the compiler placed
// here (deque iterators 0x006C5FE0, 0x006C6010, 0x006C6860, 0x006C6A30; deque
// find, fill, copy and shuffle 0x006C63C0, 0x006C6530, 0x006C6570, 0x006C6630,
// 0x006C68C0, 0x006C69D0, 0x006C6A90, 0x006C6BA0, 0x006C6C20, 0x006C6ED0,
// 0x006C6F50, 0x006C7080, 0x006C7100, 0x006C71C0; deque map growth
// 0x006C6FD0, 0x006C7020; tree erase 0x006C6080 and insert 0x006C6AE0; the
// list append 0x006C6CA0; the vector of references' destructor 0x006C6E90,
// which 23 other classes call).
//
// Fields used here (offsets from the object's start; lot_sim.h has the
// names):
//   0x18 kDevelopment      the building development simulator: slot 7 adds a
//                          building occupant (ChangeBuilding), slot 39 gives a
//                          building type's profile, wealth at +4 (0x006C60C0)
//   0x1C kCity             the city: slots 95 (position to cell), 96 (cell
//                          corner to position) and 97 (cell centre to
//                          position)
//   0x24 kLots             the lot manager: slot 6 the lot at a cell
//                          (RemoveFoundation)
//   0x28 kOccupants        the occupant manager: slot 21 removes an occupant
//   0x30 kProps            the prop manager: slot 26 adds a lot prop (type,
//                          position, facing) and returns its occupant
//   0x3C kTerrain          the terrain: slot 13 the altitude at (x, z), slot
//                          30 the water's altitude at (x, z), slot 34 whether
//                          a cell is under water
//   0x44 kSizeX, 0x48 kSizeZ  float: the city's size in metres; slot 16
//                          clamps its rectangle to them
//   0xD0 kTuningD0         float: the flatness tolerance (IsAreaFlat: every
//                          vertex within it of the first vertex)
//   0xD4 kFoundationHeight float: the slope above which a foundation is
//                          needed (ChangeBuilding, IsLotObjectFoundationRequired
//                          compare a slope of slot 29/31 with it)
//   0xD8 kTuningD8         uint32: the foundation type of a configuration
//                          that names none (AddLotFoundation, slot 54)
//   0xE8 kLevelDefault     float: passed to AttemptAreaLevelling by
//                          ChangeBuilding
//   0xEC kCurve            SC4Curve: cost per cell by slope / 16
//                          (GetLotDevelopmentCost)
//   0xFC kTree             std::map<int32, {uint32 texture +0x14, uint8
//                          +0x18, uint8 +0x19}>: the boundary textures by
//                          the four boundary values packed four bits each
//                          (GetBoundaryLotTexture; key 0 when missing)
// Statics: 0x00B49AF0 the class's random generator (foundation, texture and
// family prop picks), 0x00B49AEC a float nothing writes (0.0), the start of
// the position vectors.
//
// A lot configuration object (0x34 bytes; gzcom-dll
// cSC4LotConfigurationObject): +0 type (1 prop, 2 texture, 4 flora), +2
// rotation, +4 x, +8 y, +0xC z, +0x10 the rectangle (four floats), +0x20 the
// foundation type, +0x24 the object's ID (the occupant's lot tag), +0x28 a
// vector of IDs.
//
// Floating point: the float results (slots 16-18, 27-31, 0x006C5360) come
// back on the FPU stack unrounded, so ours return double and compute the
// intermediates in double where the game keeps them on the stack, float where
// it stores them. Every virtual call's argument count was taken from the
// callee's `ret N` (vt.py on the receiver's vtable).
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/lot_sim.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <windows.h>

#include <cfloat>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>

namespace opensc4::lot_sim {
namespace {

struct CellRect {
    int32_t x0;
    int32_t z0;
    int32_t x1;
    int32_t z1;
};

struct FloatRect {
    float x0;
    float z0;
    float x1;
    float z1;
};

struct IdVector {
    uint32_t* begin;
    uint32_t* end;
    uint32_t* capacity;
};

struct ConfigObject {
    uint8_t type;
    uint8_t flags;
    uint8_t rotation;
    uint8_t mirrored;
    float x;
    float y;
    float z;
    float rect[4];
    uint32_t foundation;
    uint32_t id;
    IdVector ids;
};
static_assert(sizeof(ConfigObject) == 0x34);

struct ObjectVector {
    ConfigObject* begin;
    ConfigObject* end;
    ConfigObject* capacity;
};

struct ListNode {
    ListNode* next;
    ListNode* previous;
    void* value;
};

using BelowFn = uint32_t(__thiscall*)(void* random, uint32_t count);
using EvaluateCurveFn = double(__thiscall*)(const void* curve, float x); // ST0, unrounded
using FindFn = void(__thiscall*)(void* map, void** out, const int32_t* key);
using FreeFn = void(__cdecl*)(void* p);
using ClearLotFn = void(__thiscall*)(void* self, void* lot, uint32_t state, uint32_t foundations, uint32_t flag);

// The truncating float-to-int conversion (_ftol, 0x009EEF04): the low dword
// of the int64.
int32_t Ftol(double value) { return static_cast<int32_t>(static_cast<int64_t>(value)); }

// A draw below `count` from the class's generator.
uint32_t Draw(uint32_t count)
{
    return reinterpret_cast<BelowFn>(addr::SC4Random_BelowLcg)(
        reinterpret_cast<void*>(addr::cSC4LotDeveloper_Random), count);
}

// The lowest (or highest) so far against a new value: the game compares the
// stored float with the unrounded value and stores the value as a float.
void Keep(float& best, double value, bool lowest)
{
    if (lowest ? static_cast<double>(best) > value : static_cast<double>(best) < value)
        best = static_cast<float>(value);
}

// --- Slot 39: the position of a lot configuration object ---------------------

// Slot 39 (0x006C4EF0): the world position and facing of a lot's
// configuration object. The lot's cell rectangle gives the corners (the
// city's slot 96); the object's x and z (snapped to cell centres for a
// texture, type 2) are laid from the corner the lot's facing names. The
// altitude: the lot's own when its flag 1 is set, else the lot foundation's
// occupant's position, else the terrain's at the spot (props, textures,
// flora) or the highest of the object's rectangle (slot 16, with water when
// the configuration is compatible with water), plus the object's y. The
// facing is the lot's plus the object's rotation, modulo 4. Returns the
// facing's address (EAX as the game leaves it).
int32_t* __fastcall GetPositionForLotConfigurationObject(void* self, void*, void* lot, void* config,
                                                         const ConfigObject* object, float* position,
                                                         int32_t* facing)
{
    static bool logged = false;
    FirstCall("GetPositionForLotConfigurationObject", logged);
    CellRect cells{};
    Virtual<uint8_t>(lot, 0x38, &cells);
    float minX = 0.0f;
    float minZ = 0.0f;
    float maxX = 0.0f;
    float maxZ = 0.0f;
    Virtual<void>(At<void*>(self, field::kCity), 0x180, cells.x0, cells.z0, &minX, &minZ);
    Virtual<void>(At<void*>(self, field::kCity), 0x180, cells.x1 + 1, cells.z1 + 1, &maxX, &maxZ);
    const int32_t lotFacing = Virtual<int32_t>(lot, 0x5C);
    // x stays on the FPU stack; z is stored.
    double x = object->x;
    const float y = object->y;
    float z = object->z;
    if (object->type == 2) {
        x = (static_cast<double>(Ftol(x * 0.0625)) + 0.5) * 16.0;
        z = static_cast<float>((static_cast<double>(Ftol(static_cast<double>(z) * 0.0625)) + 0.5) * 16.0);
    }
    const auto add = [](float a, float b) { return static_cast<float>(static_cast<double>(a) + b); };
    const auto sub = [](float a, float b) { return static_cast<float>(static_cast<double>(a) - b); };
    const float* r = object->rect;
    FloatRect area{};
    switch (static_cast<uint32_t>(lotFacing)) {
    case 0:
        position[0] = static_cast<float>(static_cast<double>(maxX) - x);
        position[2] = sub(maxZ, z);
        area = {sub(maxX, r[0]), sub(maxZ, r[1]), sub(maxX, r[2]), sub(maxZ, r[3])};
        break;
    case 1:
        position[0] = add(z, minX);
        position[2] = static_cast<float>(static_cast<double>(maxZ) - x);
        area = {add(minX, r[3]), sub(maxZ, r[0]), add(minX, r[1]), sub(maxZ, r[2])};
        break;
    case 2:
        position[0] = static_cast<float>(x + static_cast<double>(minX));
        position[2] = add(z, minZ);
        area = {add(minX, r[2]), add(minZ, r[3]), add(minX, r[0]), add(minZ, r[1])};
        break;
    case 3:
        position[0] = sub(maxX, z);
        position[2] = static_cast<float>(x + static_cast<double>(minZ));
        area = {sub(maxX, r[1]), add(minZ, r[2]), sub(maxX, r[3]), add(minZ, r[0])};
        break;
    default:
        position[0] = 0.0f;
        position[2] = 0.0f;
        area = {0.0f, 1.0f, 0.0f, 1.0f};
        break;
    }
    if (Virtual<uint32_t>(lot, 0x30) & 1) {
        position[1] = static_cast<float>(Virtual<double>(lot, 0x40));
    } else if (void* const foundation = Virtual<void*>(self, 0xCC, lot)) {
        void* const occupant = Virtual<void*>(foundation, 0xC);
        // The game's buffer lies over the rectangle's last three values.
        float at[3] = {area.x1, area.z1, area.x0};
        Virtual<uint8_t>(occupant, 0x20, at);
        position[1] = at[1];
    } else {
        const uint32_t water = config ? Virtual<uint8_t>(config, 0x38) : 0;
        const uint8_t type = object->type;
        if (type == 1 || type == 2 || type == 4) {
            position[1] = static_cast<float>(
                Virtual<double>(At<void*>(self, field::kTerrain), 0x34, position[0], position[2]));
        } else {
            position[1] = static_cast<float>(Virtual<double>(self, 0x40, &area, 0u, water));
        }
    }
    position[1] = add(y, position[1]);
    *facing = lotFacing;
    *facing = (static_cast<int32_t>(object->rotation) + lotFacing) % 4;
    return facing;
}

// --- Slot 10: a new building on a lot ----------------------------------------

// Slot 10 (0x006C5210): replaces the lot's building. Clears the lot
// (0x006C3C50: state 1, foundations too), adds the new building occupant at
// the building's position (slot 40; the development simulator's slot 7) and,
// on a lot without the flags 1 and 2, levels the ground under it when its
// slope (slot 31) is above the foundation limit; a slope still above it
// afterwards gets a building foundation (slot 58). The lot's state becomes 3.
// Nothing without a lot configuration.
uint32_t __fastcall ChangeBuilding(void* self, void*, void* lot, uint32_t building)
{
    static bool logged = false;
    FirstCall("ChangeBuilding", logged);
    void* const config = Virtual<void*>(lot, 0x24);
    if (!config)
        return 0;
    reinterpret_cast<ClearLotFn>(addr::cSC4LotDeveloper_Fn_006C3C50)(self, lot, 1, 1, 0);
    float position[3] = {}; // not initialised by the game
    int32_t facing = 0;
    Virtual<uint8_t>(self, 0xA0, lot, position, &facing);
    void* const occupant = Virtual<void*>(At<void*>(self, field::kDevelopment), 0x1C, building, position, facing);
    if (occupant) {
        const bool fixedAltitude = (Virtual<uint32_t>(lot, 0x30) & 3) != 0;
        const uint32_t water = Virtual<uint8_t>(config, 0x38);
        if (!fixedAltitude) {
            const double slope = Virtual<double>(self, 0x7C, Virtual<void*>(occupant, 0xC), water);
            const float stored = static_cast<float>(slope);
            if (slope > static_cast<double>(At<float>(self, field::kFoundationHeight))) {
                CellRect cover{}; // not initialised by the game
                Virtual<uint8_t>(self, 0x58, Virtual<void*>(occupant, 0xC), &cover);
                const float limit = At<float>(self, field::kFoundationHeight);
                const float levelLimit = At<float>(self, field::kLevelDefault);
                double after = stored;
                if (Virtual<uint32_t>(self, 0x54, &cover, limit, 1u, water, levelLimit, lot))
                    after = Virtual<double>(self, 0x7C, Virtual<void*>(occupant, 0xC), water);
                if (after > static_cast<double>(At<float>(self, field::kFoundationHeight)))
                    Virtual<uint8_t>(self, 0xE8, occupant, config);
            }
        }
    }
    return Virtual<uint32_t>(lot, 0x88, 3u, 0u, 0u);
}

// --- 0x006C5360: the altitude of a lot's foundation --------------------------

// 0x006C5360 (called by AddLotFoundation 0x006C8AB0): the altitude a lot's
// foundation is built at. A lot whose configuration is compatible with water
// and has a cell under water (the terrain's slot 34) takes the water's
// altitude at that cell's centre plus 0.5. Otherwise the middle of the two
// corner altitudes at the lot's facing and the next one (corners: four
// floats). The second and third arguments are not read.
double __fastcall FoundationAltitude(void* self, void*, void* lot, uint32_t, uint32_t, const float* corners)
{
    static bool logged = false;
    FirstCall("FoundationAltitude", logged);
    void* const config = Virtual<void*>(lot, 0x24);
    if (config && Virtual<uint8_t>(config, 0x38)) {
        void* const terrain = Global<void*>(addr::SC4_Terrain);
        CellRect cells{};
        Virtual<uint8_t>(lot, 0x38, &cells);
        for (int32_t x = cells.x0; x <= cells.x1; ++x) {
            for (int32_t z = cells.z0; z <= cells.z1; ++z) {
                if (!Virtual<uint8_t>(terrain, 0x88, x, z))
                    continue;
                float px = 0.0f;
                float pz = 0.0f;
                Virtual<void>(At<void*>(self, field::kCity), 0x184, x, z, &px, &pz);
                return Virtual<double>(terrain, 0x78, px, pz) + 0.5;
            }
        }
    }
    const int32_t facing = Virtual<int32_t>(lot, 0x5C);
    const double a = corners[facing];
    const double b = corners[(facing + 1) % 4];
    const double high = a < b ? b : a;
    const double low = a < b ? a : b;
    // The game's operand order (0x006C5406: the lower less the higher,
    // halved, added to the higher).
    return high + (low - high) * 0.5;
}

// --- Foundations -------------------------------------------------------------

// Slot 54 (0x006C5460): the lot foundation by configuration: the type is one
// of the IDs of the configuration's foundation object drawn at random, or
// that object's own type without IDs, or the default (+0xD8) without the
// object; then slot 55 with it. False without a lot, a configuration or a
// type.
uint32_t __fastcall AddLotFoundationByConfig(void* self, void*, void* lot, void* config, uint32_t a3, uint32_t a4,
                                             uint32_t a5)
{
    static bool logged = false;
    FirstCall("AddLotFoundation (configuration)", logged);
    if (!lot || !config)
        return 0;
    uint32_t type = At<uint32_t>(self, field::kTuningD8);
    if (const ConfigObject* const object = Virtual<const ConfigObject*>(config, 0x68)) {
        const uint32_t count = static_cast<uint32_t>(object->ids.end - object->ids.begin);
        type = count != 0 ? object->ids.begin[Draw(count)] : object->foundation;
    }
    if (type == 0)
        return 0;
    return Virtual<uint32_t>(self, 0xDC, lot, type, a3, a4, a5);
}

// Slot 52 (0x006C54E0): removes the foundation at a position (slot 50): its
// occupant leaves the occupant manager. For kind 2 the lot at the position's
// cell also loses its flags 1 and 2. Returns whether a foundation was there.
uint32_t __fastcall RemoveFoundation(void* self, void*, int32_t kind, float x, float z)
{
    static bool logged = false;
    FirstCall("RemoveFoundation", logged);
    uint32_t removed = 0;
    if (void* const foundation = Virtual<void*>(self, 0xC8, kind, x, z)) {
        void* const occupant = Virtual<void*>(foundation, 0xC);
        if (occupant)
            Virtual<uint32_t>(occupant, 0x4);
        Virtual<uint8_t>(At<void*>(self, field::kOccupants), 0x54, occupant, 1u, 0u);
        removed = 1;
        if (occupant)
            Virtual<uint32_t>(occupant, 0x8);
    }
    if (kind == 2) {
        void* const lots = At<void*>(self, field::kLots);
        const int32_t cellZ = Ftol(static_cast<double>(z) * 0.0625);
        const int32_t cellX = Ftol(static_cast<double>(x) * 0.0625);
        if (void* const lot = Virtual<void*>(lots, 0x18, cellX, cellZ, 0u)) {
            const uint32_t flags = Virtual<uint32_t>(lot, 0x30);
            Virtual<uint8_t>(lot, 0x34, flags & 0xFFFFFFFCu);
        }
    }
    return removed;
}

// Slot 57 (0x006C5590): whether an object between two positions needs a
// foundation: the slope (slot 29, with water) of the cells from the first
// position's to one past the second's is above the limit.
uint32_t __fastcall IsLotObjectFoundationRequired(void* self, void*, const float* from, const float* to)
{
    static bool logged = false;
    FirstCall("IsLotObjectFoundationRequired (positions)", logged);
    CellRect cells{};
    Virtual<void>(At<void*>(self, field::kCity), 0x17C, from[0], from[2], &cells.x0, &cells.z0);
    Virtual<void>(At<void*>(self, field::kCity), 0x17C, to[0], to[2], &cells.x1, &cells.z1);
    ++cells.x1;
    ++cells.z1;
    const double slope = Virtual<double>(self, 0x74, &cells, 1u);
    return slope > static_cast<double>(At<float>(self, field::kFoundationHeight)) ? 1 : 0;
}

// --- Altitudes ---------------------------------------------------------------

// Slot 17 (0x006C5620): the lowest (or highest) vertex altitude (slot 15)
// over a cell rectangle's vertices, x0..x1+1 by z0..z1+1.
double __fastcall GetLowestOrHighestCellAltitude(void* self, void*, const CellRect* cells, uint32_t lowestArg,
                                                 uint32_t water)
{
    static bool logged = false;
    FirstCall("GetLowestOrHighestAreaAltitude (cells)", logged);
    const int32_t x0 = cells->x0;
    const int32_t z0 = cells->z0;
    const int32_t x1 = cells->x1 + 1;
    const int32_t z1 = cells->z1 + 1;
    const bool lowest = static_cast<uint8_t>(lowestArg) != 0;
    float best = lowest ? FLT_MAX : -FLT_MAX;
    for (int32_t x = x0; x <= x1; ++x) {
        for (int32_t z = z0; z <= z1; ++z)
            Keep(best, Virtual<double>(self, 0x3C, x, z, water), lowest);
    }
    return best;
}

// Slot 16 (0x006C56C0): the lowest (or highest) altitude of a rectangle in
// metres, clamped to the city: the vertices strictly inside it (slot 15),
// then the four edges sampled from the terrain every 4 m and at their ends
// (slot 13, or the water's altitude of slot 30 where it is higher and the
// flag is set).
double __fastcall GetLowestOrHighestAreaAltitude(void* self, void*, const FloatRect* area, uint32_t lowestArg,
                                                 uint32_t water)
{
    static bool logged = false;
    FirstCall("GetLowestOrHighestAreaAltitude (metres)", logged);
    float x0 = area->x0;
    float z0 = area->z0;
    float x1 = area->x1;
    float z1 = area->z1;
    if (x0 < 0.0f)
        x0 = 0.0f;
    if (z0 < 0.0f)
        z0 = 0.0f;
    if (x1 >= At<float>(self, field::kSizeX))
        x1 = static_cast<float>(static_cast<double>(At<float>(self, field::kSizeX)) - static_cast<double>(0.1f));
    if (z1 >= At<float>(self, field::kSizeZ))
        z1 = static_cast<float>(static_cast<double>(At<float>(self, field::kSizeZ)) - static_cast<double>(0.1f));
    const bool lowest = static_cast<uint8_t>(lowestArg) != 0;
    const bool withWater = static_cast<uint8_t>(water) != 0;
    float best = lowest ? FLT_MAX : -FLT_MAX;
    const int32_t cx0 = Ftol((static_cast<double>(x0) + 16.0) * 0.0625);
    const int32_t cz0 = Ftol((static_cast<double>(z0) + 16.0) * 0.0625);
    const int32_t cx1 = Ftol(static_cast<double>(x1) * 0.0625);
    const int32_t cz1 = Ftol(static_cast<double>(z1) * 0.0625);
    if (cx1 >= cx0 && cz1 >= cz0) {
        for (int32_t x = cx0; x <= cx1; ++x) {
            for (int32_t z = cz0; z <= cz1; ++z)
                Keep(best, Virtual<double>(self, 0x3C, x, z, water), lowest);
        }
    }
    const auto sample = [&](float x, float z) {
        const float ground = static_cast<float>(Virtual<double>(At<void*>(self, field::kTerrain), 0x34, x, z));
        double value = ground;
        if (withWater) {
            const double surface = Virtual<double>(At<void*>(self, field::kTerrain), 0x78, x, z);
            if (surface > static_cast<double>(ground))
                value = surface;
        }
        Keep(best, value, lowest);
    };
    // One edge: from the start in steps of 4 m, the end itself once the next
    // step would pass it.
    const auto walk = [](float from, float to, const auto& at) {
        if (!(from < to))
            return;
        float current = from;
        for (;;) {
            at(current);
            if (current < to && !(static_cast<double>(current) + 4.0 < static_cast<double>(to))) {
                current = to;
                continue;
            }
            const double next = static_cast<double>(current) + 4.0;
            current = static_cast<float>(next);
            if (!(next < static_cast<double>(to)))
                break;
        }
    };
    walk(x0, x1, [&](float x) { sample(x, z0); });
    walk(z0, z1, [&](float z) { sample(x1, z); });
    walk(x0, x1, [&](float x) { sample(x, z1); });
    walk(z0, z1, [&](float z) { sample(x0, z); });
    return best;
}

// Slot 18 (0x006C5B70): the average vertex altitude over a cell rectangle's
// vertices (the sum kept as a float).
double __fastcall GetAverageAreaAltitude(void* self, void*, const CellRect* cells, uint32_t water)
{
    static bool logged = false;
    FirstCall("GetAverageAreaAltitude", logged);
    const int32_t x1 = cells->x1 + 1;
    const int32_t z0 = cells->z0;
    const int32_t z1 = cells->z1 + 1;
    float sum = 0.0f;
    int32_t count = 0;
    for (int32_t x = cells->x0; x <= x1; ++x) {
        if (z0 > z1)
            continue;
        count += z1 - z0 + 1;
        for (int32_t z = z0; z <= z1; ++z)
            sum = static_cast<float>(Virtual<double>(self, 0x3C, x, z, water) + static_cast<double>(sum));
    }
    return static_cast<double>(sum) / static_cast<double>(count);
}

// Slot 24 (0x006C5C00): whether a lot's cell rectangle is developed (slot
// 23).
uint32_t __fastcall IsLotDeveloped(void* self, void*, void* lot)
{
    static bool logged = false;
    FirstCall("IsLotDeveloped", logged);
    CellRect cells{};
    Virtual<uint8_t>(lot, 0x38, &cells);
    return Virtual<uint32_t>(self, 0x5C, &cells);
}

// Slot 25 (0x006C5C30): whether every vertex of a cell rectangle is within
// the tolerance (+0xD0) of the first vertex's altitude. The first vertex is
// always read with water.
uint32_t __fastcall IsAreaFlat(void* self, void*, const CellRect* cells, uint32_t water)
{
    static bool logged = false;
    FirstCall("IsAreaFlat", logged);
    const int32_t x0 = cells->x0;
    const int32_t z0 = cells->z0;
    const int32_t x1 = cells->x1 + 1;
    const int32_t z1 = cells->z1 + 1;
    const float first = static_cast<float>(Virtual<double>(self, 0x3C, x0, z0, 1u));
    for (int32_t x = x0; x <= x1; ++x) {
        for (int32_t z = z0; z <= z1; ++z) {
            const double difference = Virtual<double>(self, 0x3C, x, z, water) - static_cast<double>(first);
            if (std::fabs(difference) > static_cast<double>(At<float>(self, field::kTuningD0)))
                return 0;
        }
    }
    return 1;
}

// Slot 26 (0x006C5CC0): IsAreaFlat of a lot's cell rectangle.
uint32_t __fastcall IsLotFlat(void* self, void*, void* lot, uint32_t water)
{
    static bool logged = false;
    FirstCall("IsLotFlat", logged);
    CellRect cells{};
    Virtual<uint8_t>(lot, 0x38, &cells);
    return Virtual<uint32_t>(self, 0x64, &cells, water);
}

// Slot 27 (0x006C5CF0): highest minus lowest vertex altitude of a cell
// rectangle.
double __fastcall GetAreaAltitudeRange(void* self, void*, const CellRect* cells, uint32_t water)
{
    static bool logged = false;
    FirstCall("GetAreaAltitudeRange", logged);
    const int32_t x1 = cells->x1 + 1;
    const int32_t z0 = cells->z0;
    const int32_t z1 = cells->z1 + 1;
    float low = FLT_MAX;
    float high = -FLT_MAX;
    for (int32_t x = cells->x0; x <= x1; ++x) {
        for (int32_t z = z0; z <= z1; ++z) {
            const double value = Virtual<double>(self, 0x3C, x, z, water);
            Keep(low, value, true);
            Keep(high, value, false);
        }
    }
    return static_cast<double>(high) - static_cast<double>(low);
}

// Slot 28 (0x006C5D90): the altitude range of a lot's cell rectangle.
double __fastcall GetLotAltitudeRange(void* self, void*, void* lot, uint32_t water)
{
    static bool logged = false;
    FirstCall("GetLotAltitudeRange", logged);
    CellRect cells{};
    Virtual<uint8_t>(lot, 0x38, &cells);
    return Virtual<double>(self, 0x6C, &cells, water);
}

// Slot 29 (0x006C5DC0): the slope of a cell rectangle: the altitude range
// over the distance in vertices between the lowest and the highest vertex
// (the first of each in x, z order). The distance is scaled down in steps
// when long: above 6 by 0.25, then above 5 by 0.3, then above 4 by 0.35,
// else above 3 by 0.4. 0 when both are the same vertex.
double __fastcall GetAreaSlope(void* self, void*, const CellRect* cells, uint32_t water)
{
    static bool logged = false;
    FirstCall("GetAreaSlope", logged);
    const int32_t x1 = cells->x1 + 1;
    const int32_t z0 = cells->z0;
    const int32_t z1 = cells->z1 + 1;
    float low = FLT_MAX;
    float high = -FLT_MAX;
    // Not initialised by the game (always written: the rectangle has a vertex).
    float lowAt[2] = {};
    float highAt[2] = {};
    for (int32_t x = cells->x0; x <= x1; ++x) {
        for (int32_t z = z0; z <= z1; ++z) {
            const double value = Virtual<double>(self, 0x3C, x, z, water);
            if (static_cast<double>(low) > value) {
                low = static_cast<float>(value);
                lowAt[0] = static_cast<float>(x);
                lowAt[1] = static_cast<float>(z);
            }
            if (static_cast<double>(high) < value) {
                high = static_cast<float>(value);
                highAt[0] = static_cast<float>(x);
                highAt[1] = static_cast<float>(z);
            }
        }
    }
    // The distance between the two (the game's 2D helper 0x005D76A0).
    const double dx = static_cast<double>(highAt[0]) - static_cast<double>(lowAt[0]);
    const double dz = static_cast<double>(highAt[1]) - static_cast<double>(lowAt[1]);
    double distance = std::sqrt(dz * dz + dx * dx);
    if (distance == 0.0)
        return 0.0;
    if (distance > 6.0)
        distance *= 0.25;
    if (distance > 5.0)
        distance *= static_cast<double>(0.3f);
    if (distance > 4.0)
        distance *= static_cast<double>(0.35f);
    else if (distance > 3.0)
        distance *= static_cast<double>(0.4f);
    return (static_cast<double>(high) - static_cast<double>(low)) / distance;
}

// Slot 30 (0x006C5F20): the slope of a lot's cell rectangle.
double __fastcall GetLotSlope(void* self, void*, void* lot, uint32_t water)
{
    static bool logged = false;
    FirstCall("GetLotSlope", logged);
    CellRect cells{};
    Virtual<uint8_t>(lot, 0x38, &cells);
    return Virtual<double>(self, 0x74, &cells, water);
}

// Slot 31 (0x006C5F50): the slope of the cells an occupant covers (slot 22).
double __fastcall GetOccupantSlope(void* self, void*, void* occupant, uint32_t water)
{
    static bool logged = false;
    FirstCall("GetOccupantSlope", logged);
    CellRect cells{}; // not initialised by the game
    Virtual<uint8_t>(self, 0x58, occupant, &cells);
    return Virtual<double>(self, 0x74, &cells, water);
}

// Slot 32 (0x006C5F80): the cost of developing a cell rectangle: the curve
// (+0xEC) at its slope (with water) / 16, times its cells, truncated (int64
// in EDX:EAX).
int64_t __fastcall GetLotDevelopmentCost(void* self, void*, const CellRect* cells)
{
    static bool logged = false;
    FirstCall("GetLotDevelopmentCost", logged);
    const int32_t width = cells->x1 - cells->x0 + 1;
    const int32_t depth = cells->z1 - cells->z0 + 1;
    const float slope = static_cast<float>(Virtual<double>(self, 0x74, cells, 1u) * 0.0625);
    double cost = reinterpret_cast<EvaluateCurveFn>(addr::SC4Curve_Evaluate)(
        static_cast<char*>(self) + field::kCurve, slope);
    cost = cost * static_cast<double>(depth);
    cost = cost * static_cast<double>(width);
    return static_cast<int64_t>(cost);
}

// --- Textures and props ------------------------------------------------------

// The instance IDs the base texture pick tries: texture resources (type
// 0x7AB50E44, group 0x0986135E).
bool HasTexture(uint32_t instance)
{
    uint32_t key[3] = {0x7AB50E44, 0x0986135E, instance};
    return Virtual<uint8_t>(Global<void*>(addr::SC4_ResourceManager), 0x38, key) != 0;
}

// 0x006C60C0 (called by SetNormalLotBaseTexture 0x006CA8A0): picks a base
// texture for a lot from a vector of IDs: one at random, then the variant of
// its upper 16 bits for the lot's wealth (the lot's slot 41; without one the
// wealth of the lot's building type, or 1) in bits 12-15 and the lot's
// condition tier (slot 35: 0-1 none, 2-3 one, 4-6 two) in bits 8-11 when the
// resource manager has it; else wealth alone, else tier alone, else the ID as
// drawn. 0 for an empty vector.
uint32_t __fastcall PickBaseTexture(void* self, void*, void* lot, const IdVector* ids)
{
    static bool logged = false;
    FirstCall("PickBaseTexture", logged);
    if (ids->begin == ids->end)
        return 0;
    const uint32_t drawn = ids->begin[Draw(static_cast<uint32_t>(ids->end - ids->begin))];
    uint32_t wealth = Virtual<uint32_t>(lot, 0xA4);
    uint32_t tier = 0;
    switch (Virtual<uint32_t>(lot, 0x8C)) {
    case 2:
    case 3:
        tier = 1;
        break;
    case 4:
    case 5:
    case 6:
        tier = 2;
        break;
    default:
        break;
    }
    if (wealth == 0) {
        if (const uint32_t type = Virtual<uint32_t>(lot, 0xB8, 0u))
            wealth = At<uint32_t>(Virtual<void*>(At<void*>(self, field::kDevelopment), 0x9C, type), 0x4);
        else
            wealth = 1;
    }
    const uint32_t base = drawn & 0xFFFF0000u;
    const uint32_t both = (((wealth << 4) | tier) << 8) | base;
    if (HasTexture(both))
        return both;
    const uint32_t byWealth = (wealth << 12) | base;
    if (HasTexture(byWealth))
        return byWealth;
    const uint32_t byTier = (tier << 8) | base;
    if (HasTexture(byTier))
        return byTier;
    return drawn;
}

// The start of slot 63 and 0x006C6D70: the configuration object by index and
// its position. False without a lot or a configuration.
bool ObjectPlace(void* self, void* lot, int32_t index, ConfigObject*& object, float* position, int32_t& facing)
{
    if (!lot)
        return false;
    void* const config = Virtual<void*>(lot, 0x24);
    if (!config)
        return false;
    object = Virtual<ObjectVector*>(config, 0x5C)->begin + index;
    const float start = Global<float>(addr::cSC4LotDeveloper_ZeroFloat);
    position[0] = start;
    position[1] = start;
    position[2] = start;
    Virtual<void*>(self, 0x9C, lot, config, object, position, &facing);
    return true;
}

// Slot 63 (0x006C6230): a prop of a given type for a lot's configuration
// object (by index): placed at the object's position (slot 39) through the
// prop manager and tagged with the object's ID. Null without a lot, a
// configuration, a type or the prop.
void* __fastcall CreateLotProp(void* self, void*, void* lot, int32_t index, uint32_t type)
{
    static bool logged = false;
    FirstCall("CreateLotProp", logged);
    ConfigObject* object = nullptr;
    float position[3];
    int32_t facing = 0;
    if (!ObjectPlace(self, lot, index, object, position, facing))
        return nullptr;
    if (type == 0)
        return nullptr;
    void* const occupant = Virtual<void*>(At<void*>(self, field::kProps), 0x68, type, position, facing);
    if (!occupant)
        return nullptr;
    Virtual<uint8_t>(occupant, 0x70, object->id);
    return occupant;
}

// 0x006C6D70 (called by 0x006C7860's loop over a lot's objects): the same
// with the type drawn at random from the IDs the lot configuration manager
// (0x00B43D18 slot 27) gives for the object; nothing for no IDs or a zero ID.
void* __fastcall CreateLotPropFromObject(void* self, void*, void* lot, int32_t index)
{
    static bool logged = false;
    FirstCall("CreateLotPropFromObject", logged);
    ConfigObject* object = nullptr;
    float position[3];
    int32_t facing = 0;
    if (!ObjectPlace(self, lot, index, object, position, facing))
        return nullptr;
    IdVector ids{nullptr, nullptr, nullptr};
    Virtual<void>(Global<void*>(addr::SC4_Service43D18), 0x6C, object, &ids);
    void* occupant = nullptr;
    if (ids.begin != ids.end) {
        const uint32_t type = ids.begin[Draw(static_cast<uint32_t>(ids.end - ids.begin))];
        if (type != 0) {
            occupant = Virtual<void*>(At<void*>(self, field::kProps), 0x68, type, position, facing);
            if (occupant)
                Virtual<uint8_t>(occupant, 0x70, object->id);
        }
    }
    if (ids.begin)
        reinterpret_cast<FreeFn>(addr::SC4_Free)(ids.begin);
    return occupant;
}

// 0x006C62F0 (called by 0x006CAAF0): shuts down and releases every occupant
// of a list (the argument points at the list's head pointer); those with the
// flag 1 leave the occupant manager first.
void __fastcall RemoveListedOccupants(void* self, void*, ListNode** list)
{
    static bool logged = false;
    FirstCall("RemoveListedOccupants", logged);
    ListNode* const head = *list;
    for (ListNode* node = head->next; node != head; node = node->next) {
        void* const occupant = node->value;
        if (Virtual<uint8_t>(occupant, 0x80, 1u))
            Virtual<uint8_t>(At<void*>(self, field::kOccupants), 0x54, occupant, 0u, 0u);
        Virtual<uint8_t>(occupant, 0x10);
        Virtual<uint32_t>(occupant, 0x8);
    }
}

// Slot 35 (0x006C6340): the travel populations of a business unit's lots:
// every member lot's is reset (the lot's slot 57), then every lot adds
// itself (slot 58) to its travel designate (slot 59), or to itself without
// one.
void __fastcall UpdateTravelPopulations(void*, void*, void* unit)
{
    static bool logged = false;
    FirstCall("UpdateTravelPopulations", logged);
    ListNode** const members = Virtual<ListNode**>(unit, 0x14);
    for (ListNode* node = (*members)->next; node != *members; node = node->next)
        Virtual<uint8_t>(node->value, 0xE4);
    for (ListNode* node = (*members)->next; node != *members; node = node->next) {
        void* const lot = node->value;
        void* const designate = Virtual<void*>(lot, 0xEC);
        Virtual<uint8_t>(designate ? designate : lot, 0xE8, lot);
    }
}

// Slot 42 (0x006C6CF0): the boundary texture for four boundary values
// (packed four bits each into the key of the map at +0xFC): the texture and
// two bytes of the entry, or of entry 0 when the key has none.
uint32_t __fastcall GetBoundaryLotTexture(void* self, void*, const uint32_t* boundary, uint32_t* texture,
                                          uint8_t* first, uint8_t* second)
{
    static bool logged = false;
    FirstCall("GetBoundaryLotTexture", logged);
    void* const map = static_cast<char*>(self) + field::kTree;
    uint32_t packed = (boundary[0] << 4) + boundary[1];
    packed = (packed << 4) + boundary[2];
    packed = (packed << 4) + boundary[3];
    int32_t key = static_cast<int32_t>(packed);
    void* node = nullptr;
    reinterpret_cast<FindFn>(addr::SC4Sint32Map_Find)(map, &node, &key);
    if (node == At<void*>(map, 0)) {
        key = 0;
        reinterpret_cast<FindFn>(addr::SC4Sint32Map_Find)(map, &node, &key);
    }
    *texture = At<uint32_t>(node, 0x14);
    *first = At<uint8_t>(node, 0x18);
    const uint8_t last = At<uint8_t>(node, 0x19);
    *second = last;
    return last;
}

// --- Installation ------------------------------------------------------------

struct Slot {
    int index;
    std::uintptr_t original;
    void* ours;
};

#define OPENSC4_SLOT(index, original, fn) {index, original, reinterpret_cast<void*>(&fn)}

const Slot kSlots[] = {
    OPENSC4_SLOT(10, 0x006C5210, ChangeBuilding),
    OPENSC4_SLOT(16, 0x006C56C0, GetLowestOrHighestAreaAltitude),
    OPENSC4_SLOT(17, 0x006C5620, GetLowestOrHighestCellAltitude),
    OPENSC4_SLOT(18, 0x006C5B70, GetAverageAreaAltitude),
    OPENSC4_SLOT(24, 0x006C5C00, IsLotDeveloped),
    OPENSC4_SLOT(25, 0x006C5C30, IsAreaFlat),
    OPENSC4_SLOT(26, 0x006C5CC0, IsLotFlat),
    OPENSC4_SLOT(27, 0x006C5CF0, GetAreaAltitudeRange),
    OPENSC4_SLOT(28, 0x006C5D90, GetLotAltitudeRange),
    OPENSC4_SLOT(29, 0x006C5DC0, GetAreaSlope),
    OPENSC4_SLOT(30, 0x006C5F20, GetLotSlope),
    OPENSC4_SLOT(31, 0x006C5F50, GetOccupantSlope),
    OPENSC4_SLOT(32, 0x006C5F80, GetLotDevelopmentCost),
    OPENSC4_SLOT(35, 0x006C6340, UpdateTravelPopulations),
    OPENSC4_SLOT(39, 0x006C4EF0, GetPositionForLotConfigurationObject),
    OPENSC4_SLOT(42, 0x006C6CF0, GetBoundaryLotTexture),
    OPENSC4_SLOT(52, 0x006C54E0, RemoveFoundation),
    OPENSC4_SLOT(54, 0x006C5460, AddLotFoundationByConfig),
    OPENSC4_SLOT(57, 0x006C5590, IsLotObjectFoundationRequired),
    OPENSC4_SLOT(63, 0x006C6230, CreateLotProp),
};

#undef OPENSC4_SLOT

// The helpers the build part calls directly. Their call sites (0x006C8B68,
// 0x006CA9AF, 0x006CAB99, 0x006C78B5) keep nothing in ECX, EDX or EAX across
// the call, so plain detours do.
struct Detour {
    std::uintptr_t address;
    void* ours;
    void* original;
};

#define OPENSC4_DETOUR(address, fn) {address, reinterpret_cast<void*>(&fn), nullptr}

Detour g_detours[] = {
    OPENSC4_DETOUR(addr::cSC4LotDeveloper_Fn_006C5360, FoundationAltitude),
    OPENSC4_DETOUR(addr::cSC4LotDeveloper_Fn_006C60C0, PickBaseTexture),
    OPENSC4_DETOUR(addr::cSC4LotDeveloper_Fn_006C62F0, RemoveListedOccupants),
    OPENSC4_DETOUR(addr::cSC4LotDeveloper_Fn_006C6D70, CreateLotPropFromObject),
};

#undef OPENSC4_DETOUR

// Checks that every slot still holds the original, installs the detours,
// then swaps the slots.
bool Install(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    auto* const vtable = reinterpret_cast<std::uintptr_t*>(addr::cSC4LotDeveloper_vftable);
    for (const Slot& slot : kSlots) {
        if (vtable[slot.index] != slot.original) {
            log::Write("lot developer terrain: slot %d of %08X holds %08X, expected %08X; not installing",
                       slot.index, static_cast<unsigned>(addr::cSC4LotDeveloper_vftable),
                       static_cast<unsigned>(vtable[slot.index]), static_cast<unsigned>(slot.original));
            return false;
        }
    }
    for (Detour& detour : g_detours) {
        const addr::Entry* entry = addr_check::Find(detour.address);
        if (!entry || !hook::Install(*entry, detour.ours, &detour.original)) {
            log::Write("lot developer terrain: no detour at %08X", static_cast<unsigned>(detour.address));
            return false;
        }
    }
    for (const Slot& slot : kSlots) {
        std::uintptr_t* const entry = vtable + slot.index;
        DWORD old = 0;
        if (!VirtualProtect(entry, sizeof(*entry), PAGE_READWRITE, &old))
            return false;
        *entry = reinterpret_cast<std::uintptr_t>(slot.ours);
        VirtualProtect(entry, sizeof(*entry), old, &old);
    }
    log::Write("lot developer terrain: %u slots and %u detours are ours", static_cast<unsigned>(std::size(kSlots)),
               static_cast<unsigned>(std::size(g_detours)));
    return true;
}

const bool g_registered = registry::Register(
    {"simulators.lot.terrain", "simulators/lot", registry::Kind::Vtable, registry::Phase::PostAppInit,
     addr::cSC4LotDeveloper_vftable,
     "Our cSC4LotDeveloper terrain part: object positions, area altitudes, range, slope and flatness, "
     "foundation by configuration, development cost, lot props, travel populations, boundary textures",
     false, Install});

} // namespace
} // namespace opensc4::lot_sim
