// Our cSC4BuildingDevelopmentSimulator's occupant and construction functions
// (build 641; docs/development.md): adding a building at a cell or a
// position, handing an occupant to the occupant manager, the foundation key
// of a type, adding a foundation, loading foundation and retaining wall
// properties, the construction check and plopping a building. Every function
// is a replacement on the original object (slots of the primary vtable
// 0x00AB0AF8); the helpers they call inside the class are plain functions
// here.
//
// Fields used here (offsets from the object's start):
//   kCity = 0x1C (SC4::City; slot 0x184 = a cell's centre in metres)
//   kOccupantManager = 0x20 (the city's slot 0x88; slot 0x50 adds an occupant)
//   kResourceManager = 0x24 (SC4::ResourceManager; slot 0xC gets a resource)
//   kZones = 0x28 (SC4::Service43D14; slot 0x30 = the zone at x, z, int8)
//   kLots = 0x2C (SC4::LotManager; slot 0x18 = the lot at x, z)
//   kWealth = 0x30 (SC4::Service43D88; slots 0x14 and 0x18 at x, z)
//   kPower = 0x34 (SC4::Service43DA0; slot 0x4C = a yes/no at x, z)
//   kTerrain = 0x38 (SC4::Terrain; slot 0x34 = altitude at x, z)
//   kCellsX = 0x40, kCellsZ = 0x44 (int32, the city's size in cells)
//   kFlatten = 0x6C (the service at 0x00B43D20; slot 0x40 = a height for a
//     float rectangle in metres, slot 0x44 for a cell rectangle)
//   kIgnorePower = 0xDE (uint8, the CheatIgnorePower flag)
// A construction profile: +0 zone purpose (int32), +8 the value slot 0x14
// of kWealth must match, +0x10 and +0x11 the lowest and highest value of
// kWealth's slot 0x18 (uint8), +0x14 building type, +0x18 rotation,
// +0x1C cell x, +0x20 cell z.
// The type -> resource key hash maps (0x00B4854C buildings, 0x00B485BC
// foundations, 0x00B485D0 retaining walls) are bucket vectors (begin, end)
// of chained nodes: +0 next, +4 type, +8 the 12-byte key.
#include "core/development_sim.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace opensc4::development_sim {
namespace {

namespace local {
constexpr std::size_t kCity = 0x1C;
constexpr std::size_t kOccupantManager = 0x20;
constexpr std::size_t kResourceManager = 0x24;
constexpr std::size_t kZones = 0x28;
constexpr std::size_t kLots = 0x2C;
constexpr std::size_t kWealth = 0x30;
constexpr std::size_t kPower = 0x34;
constexpr std::size_t kTerrain = 0x38;
constexpr std::size_t kCellsX = 0x40;
constexpr std::size_t kCellsZ = 0x44;
constexpr std::size_t kFlatten = 0x6C;
constexpr std::size_t kIgnorePower = 0xDE;
} // namespace local

// Interface IDs and properties.
constexpr uint32_t kPropertyLoaderIid = 0x0AC2B5F7; // what loads an exemplar into an occupant
constexpr uint32_t kExemplarIid = 0xA52160F5;
constexpr uint32_t kOffsetProperty = 0x27812810;     // building property read through variant slot 0x1BC
constexpr uint32_t kFoundationProperty = 0x88FCC3E3; // float handed to the foundation's slot 0x1C

using AllocateFn = void*(__cdecl*)(std::size_t size);
using ConstructFn = void*(__thiscall*)(void* memory);
using GetFloatFn = uint8_t(__cdecl*)(void* holder, uint32_t property, void* value);

// __ftol2: truncation toward zero (EDX:EAX), the low 32 bits.
int32_t FtolLow(double value) { return static_cast<int32_t>(static_cast<int64_t>(value)); }

// A float copied through FLD/FSTP: the same bits, except that a signalling
// NaN comes out quiet.
uint32_t X87Copy(uint32_t bits)
{
    if ((bits & 0x7F800000u) == 0x7F800000u && (bits & 0x007FFFFFu) != 0)
        bits |= 0x00400000u;
    return bits;
}

uint32_t Bits(float value)
{
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

// The node of `type` in one of the type -> key hash maps, or null.
char* FindType(std::uintptr_t map, uint32_t type)
{
    char** const buckets = Global<char**>(map);
    const auto count = static_cast<uint32_t>(
        static_cast<int32_t>(reinterpret_cast<std::uintptr_t>(Global<char**>(map + 4)) -
                             reinterpret_cast<std::uintptr_t>(buckets)) >>
        2);
    char* node = buckets[type % count];
    while (node) {
        if (At<uint32_t>(node, 4) == type)
            return node;
        node = At<char*>(node, 0);
    }
    return nullptr;
}

// --- Helpers inside the class ---------------------------------------------

// 0x00695390 (cdecl): whether a lot of zone `zone` suits a building of zone
// purpose `purpose`. Also called from outside the class (0x006B4D5A,
// 0x00707ADF, 0x007081BF, 0x0070880B, 0x00708CD6, 0x0070B78C).
bool ZoneSuits(int32_t purpose, int32_t zone)
{
    if (purpose == zone)
        return true;
    switch (zone) {
    case 2:
        return purpose == 1;
    case 3:
        return purpose == 1 || purpose == 2;
    case 5:
        return purpose == 4;
    case 6:
        return purpose == 4 || purpose == 5;
    case 9:
        return purpose == 8;
    case 11:
    case 12:
    case 15:
        return purpose == 0;
    default:
        return false;
    }
}

// 0x00695610: a rectangle in metres (x0, z0, x1, z1) to cells, clamped to
// the city.
void RectToCells(void* self, const float* rect, int32_t* cells)
{
    // The scale is read again for each product, as the game does.
    for (int i = 0; i < 4; ++i)
        cells[i] = FtolLow(static_cast<double>(Global<float>(addr::cSC4BuildingDevelopmentSimulator_MetresToCells)) *
                           static_cast<double>(rect[i]));
    if (cells[0] < 0)
        cells[0] = 0;
    if (cells[1] < 0)
        cells[1] = 0;
    if (cells[2] >= At<int32_t>(self, local::kCellsX))
        cells[2] = At<int32_t>(self, local::kCellsX) - 1;
    if (cells[3] >= At<int32_t>(self, local::kCellsZ))
        cells[3] = At<int32_t>(self, local::kCellsZ) - 1;
}

// 0x00698490: loads a building type's exemplar into a new building occupant
// and applies its offset property. Always true.
uint8_t LoadBuildingProperties(void* self, void* occupant, uint32_t type)
{
    void* const owner = Virtual<void*>(occupant, 0xC);
    Virtual<void*>(owner, 0x18); // result unused, as in the game
    void* loader = nullptr;
    Virtual<uint8_t>(owner, 0x0, kPropertyLoaderIid, &loader);
    if (type == 0)
        type = Virtual<uint32_t>(occupant, 0x10);
    if (char* const node = FindType(addr::cSC4BuildingDevelopmentSimulator_BuildingTypeKeys, type)) {
        // The game hands its argument slot (still holding the type) as the out pointer.
        void* exemplar = reinterpret_cast<void*>(static_cast<std::uintptr_t>(type));
        if (Virtual<uint8_t>(At<void*>(self, local::kResourceManager), 0xC, static_cast<void*>(node + 8), kExemplarIid,
                             &exemplar, 0u, 0u)) {
            Virtual<uint32_t>(loader, 0xC, exemplar);
            void* const holder = Virtual<void*>(owner, 0x18);
            if (void* const property = Virtual<void*>(holder, 0x24, kOffsetProperty)) {
                float offset[3] = {0.0f, 0.0f, 0.0f};
                void* const variant = Virtual<void*>(property, 0x14);
                const uint32_t value = Virtual<uint32_t>(variant, 0x1BC);
                Virtual<uint32_t>(occupant, 0x20, static_cast<void*>(offset), value);
            }
            Virtual<uint32_t>(exemplar, 0x8);
        }
    }
    if (loader)
        Virtual<uint32_t>(loader, 0x8);
    return 1;
}

// --- Slots -------------------------------------------------------------------

// Slots 8 and 24 (0x00695740): hands an occupant to the occupant manager.
uint8_t __fastcall AddOccupant(void* self, void*, void* occupant)
{
    static bool logged = false;
    FirstCall("AddOccupant", logged);
    void* const manager = At<void*>(self, local::kOccupantManager);
    if (!manager)
        return 0;
    void** const vtable = *static_cast<void***>(manager); // read before the occupant call, as the game does
    // The occupant's slot 3 takes no argument: the game's PUSH 0 before it
    // is the manager call's second argument, pushed early.
    void* const unknown = Virtual<void*>(occupant, 0xC);
    using AddFn = uint8_t(__thiscall*)(void*, void*, uint32_t);
    return reinterpret_cast<AddFn>(vtable[0x50 / 4])(At<void*>(self, local::kOccupantManager), unknown, 0u);
}

// Slot 7 (0x00698DE0): makes a building of `type` at a position (x, y, z;
// y below 0 = on the ground) with a rotation; the new occupant or null.
void* __fastcall AddBuildingAt(void* self, void*, uint32_t type, float* position, int32_t rotation)
{
    static bool logged = false;
    FirstCall("AddBuildingAt", logged);
    if (!Virtual<uint8_t>(self, 0x3C, type))
        return nullptr;
    const uint32_t* const raw = reinterpret_cast<const uint32_t*>(position);
    if (!Virtual<uint8_t>(self, 0x14, type, raw[0], raw[2]))
        return nullptr;
    void* const memory = reinterpret_cast<AllocateFn>(addr::cSC4BuildingOccupant_Allocate)(0x5C);
    void* const occupant =
        memory ? reinterpret_cast<ConstructFn>(addr::cSC4BuildingOccupant_cSC4BuildingOccupant)(memory) : nullptr;
    Virtual<uint32_t>(occupant, 0x4);
    Virtual<uint32_t>(occupant, 0x14, type);
    void* result = nullptr;
    if (LoadBuildingProperties(self, occupant, type)) {
        void* const placed = static_cast<char*>(occupant) + 4;
        uint32_t box[6]; // low x, y, z; high x, y, z (the game's own locals, set by the call)
        Virtual<uint32_t>(placed, 0x28, static_cast<void*>(&box[0]), static_cast<void*>(&box[3]));
        if (rotation == 1 || rotation == 3) {
            const uint32_t lowZ = box[2];
            const uint32_t highZ = box[5];
            box[2] = X87Copy(box[0]);
            box[0] = lowZ;
            box[5] = X87Copy(box[3]);
            box[3] = highZ;
        }
        Virtual<uint32_t>(occupant, 0x20, static_cast<void*>(&box[0]), static_cast<void*>(&box[3]));
        if (position[1] < 0.0f) {
            uint32_t rect[4] = {box[0], box[2], box[3], box[5]};
            position[1] = static_cast<float>(
                Virtual<double>(At<void*>(self, local::kFlatten), 0x40, static_cast<void*>(rect), 0u, 1u));
        }
        Virtual<uint32_t>(placed, 0x24, static_cast<void*>(position));
        Virtual<uint32_t>(occupant, 0x3C, rotation);
        Virtual<uint32_t>(placed, 0xC);
        if (Virtual<uint8_t>(self, 0x20, occupant))
            result = occupant;
    }
    Virtual<uint32_t>(occupant, 0x8);
    return result;
}

// Slot 6 (0x006956A0): makes a building of `type` at the centre of a cell,
// on the ground.
void* __fastcall AddBuildingAtCell(void* self, void*, uint32_t type, int32_t cellX, int32_t cellZ, int32_t rotation)
{
    static bool logged = false;
    FirstCall("AddBuildingAtCell", logged);
    // The game's x and z are uninitialised locals that the city's slot fills in.
    uint32_t position[3] = {0, 0, 0};
    Virtual<uint32_t>(At<void*>(self, local::kCity), 0x184, cellX, cellZ, static_cast<void*>(&position[0]),
                      static_cast<void*>(&position[2]));
    position[1] = Bits(static_cast<float>(
        Virtual<double>(At<void*>(self, local::kTerrain), 0x34, position[0], position[2])));
    return Virtual<void*>(self, 0x1C, type, static_cast<void*>(position), rotation);
}

// Slot 22 (0x00698120): the resource key of a foundation type.
uint8_t __fastcall FoundationKeyOfType(void*, void*, uint32_t type, uint32_t* key)
{
    static bool logged = false;
    FirstCall("FoundationKeyOfType", logged);
    char* const node = FindType(addr::cSC4BuildingDevelopmentSimulator_FoundationTypeKeys, type);
    if (!node)
        return 0;
    key[0] = At<uint32_t>(node, 0x8);
    key[1] = At<uint32_t>(node, 0xC);
    key[2] = At<uint32_t>(node, 0x10);
    return 1;
}

// Slot 26 (0x006982B0): loads a foundation type's exemplar into a
// foundation occupant (type 0 = the occupant's own).
uint8_t __fastcall LoadFoundationProperties(void* self, void*, void* occupant, uint32_t type)
{
    static bool logged = false;
    FirstCall("LoadFoundationProperties", logged);
    uint8_t result = 0;
    void* const owner = Virtual<void*>(occupant, 0xC);
    void* const holder = Virtual<void*>(owner, 0x18);
    void* loader = nullptr;
    Virtual<uint8_t>(owner, 0x0, kPropertyLoaderIid, &loader);
    if (type == 0)
        type = Virtual<uint32_t>(occupant, 0x10);
    if (char* const node = FindType(addr::cSC4BuildingDevelopmentSimulator_FoundationTypeKeys, type)) {
        void* exemplar = nullptr; // an uninitialised local in the game, only written by the call
        if (Virtual<uint8_t>(At<void*>(self, local::kResourceManager), 0xC, static_cast<void*>(node + 8), kExemplarIid,
                             &exemplar, 0u, 0u)) {
            Virtual<uint32_t>(loader, 0xC, exemplar);
            uint32_t value = 0;
            if (reinterpret_cast<GetFloatFn>(addr::SC4_GetPropertyFloat)(holder, kFoundationProperty, &value))
                Virtual<uint32_t>(occupant, 0x1C, value);
            Virtual<uint32_t>(exemplar, 0x8);
            result = 1;
        }
    }
    if (loader)
        Virtual<uint32_t>(loader, 0x8);
    return result;
}

// Slot 29 (0x006983C0): loads a retaining wall type's exemplar into a
// retaining wall occupant (type 0 = the occupant's own).
uint8_t __fastcall LoadLotRetainingWallProperties(void* self, void*, void* occupant, uint32_t type)
{
    static bool logged = false;
    FirstCall("LoadLotRetainingWallProperties", logged);
    uint8_t result = 0;
    void* const owner = Virtual<void*>(occupant, 0xC);
    Virtual<void*>(owner, 0x18); // result unused, as in the game
    void* loader = nullptr;
    Virtual<uint8_t>(owner, 0x0, kPropertyLoaderIid, &loader);
    if (type == 0)
        type = Virtual<uint32_t>(occupant, 0x2C);
    if (char* const node = FindType(addr::cSC4BuildingDevelopmentSimulator_RetainingWallTypeKeys, type)) {
        // The game hands its argument slot (still holding the type) as the out pointer.
        void* exemplar = reinterpret_cast<void*>(static_cast<std::uintptr_t>(type));
        if (Virtual<uint8_t>(At<void*>(self, local::kResourceManager), 0xC, static_cast<void*>(node + 8), kExemplarIid,
                             &exemplar, 0u, 0u)) {
            Virtual<uint32_t>(loader, 0xC, exemplar);
            Virtual<uint32_t>(exemplar, 0x8);
            result = 1;
        }
    }
    if (loader)
        Virtual<uint32_t>(loader, 0x8);
    return result;
}

// Slot 23 (0x00695AF0): makes a foundation of `type` at a position (y below
// 0 = on the ground) with a width, depth, rotation and height (below 0 =
// down to the ground; at least 0.5).
uint8_t __fastcall AddFoundationOccupant(void* self, void*, uint32_t type, int32_t unknown, float* position, float width,
                                         float depth, int32_t rotation, float height)
{
    static bool logged = false;
    FirstCall("AddFoundationOccupant", logged);
    uint8_t result = 0;
    void* const memory = reinterpret_cast<AllocateFn>(addr::cSC4FoundationOccupant_Allocate)(0x5C);
    void* const occupant =
        memory ? reinterpret_cast<ConstructFn>(addr::cSC4FoundationOccupant_cSC4FoundationOccupant)(memory) : nullptr;
    Virtual<uint32_t>(occupant, 0x4);
    Virtual<uint32_t>(occupant, 0x14, type);
    if (Virtual<uint8_t>(self, 0x68, occupant, type)) {
        // The footprint in metres, half a metre inside each edge.
        const double halfWidth = static_cast<double>(width) * 0.5;
        const double halfDepth = static_cast<double>(depth) * 0.5;
        float rect[4];
        rect[0] = static_cast<float>((static_cast<double>(position[0]) - halfWidth) - -0.5);
        rect[1] = static_cast<float>(static_cast<double>(position[2]) - halfDepth);
        rect[2] = static_cast<float>(static_cast<double>(position[0]) + halfWidth);
        rect[3] = static_cast<float>(static_cast<double>(position[2]) + halfDepth);
        rect[1] = static_cast<float>(static_cast<double>(rect[1]) - -0.5);
        rect[2] = static_cast<float>(static_cast<double>(rect[2]) - 0.5);
        rect[3] = static_cast<float>(static_cast<double>(rect[3]) - 0.5);
        int32_t cells[4];
        RectToCells(self, rect, cells);
        if (position[1] < 0.0f)
            position[1] = static_cast<float>(
                Virtual<double>(At<void*>(self, local::kFlatten), 0x44, static_cast<void*>(cells), 0u, 1u));
        double base;
        if (!(height >= 0.0f))
            base = Virtual<double>(At<void*>(self, local::kFlatten), 0x44, static_cast<void*>(cells), 1u, 1u);
        else
            base = static_cast<double>(height);
        const double rise = static_cast<double>(position[1]) - base;
        const uint32_t size[3] = {Bits(width), rise < 0.5 ? Bits(0.5f) : Bits(static_cast<float>(rise)), Bits(depth)};
        void* const placed = static_cast<char*>(occupant) + 4;
        Virtual<uint32_t>(placed, 0x24, static_cast<void*>(position));
        Virtual<uint32_t>(occupant, 0x20, static_cast<const void*>(size));
        Virtual<uint32_t>(occupant, 0x28, unknown);
        Virtual<uint32_t>(occupant, 0x30, rotation);
        Virtual<uint32_t>(placed, 0xC);
        result = Virtual<uint8_t>(self, 0x60, occupant);
    }
    Virtual<uint32_t>(occupant, 0x8);
    return result;
}

// Slot 46 (0x00695430): whether a construction profile can be built; if not,
// why (0 zone, 1 wealth range, 2 wealth, 3 no lot, 4 lot state, 5 no power).
uint8_t __fastcall IsSuitableForConstruction(void* self, void*, char* profile, int32_t* reason)
{
    static bool logged = false;
    FirstCall("IsSuitableForConstruction", logged);
    const auto x = [profile] { return At<uint32_t>(profile, 0x1C); };
    const auto z = [profile] { return At<uint32_t>(profile, 0x20); };
    const int8_t zone = Virtual<int8_t>(At<void*>(self, local::kZones), 0x30, x(), z());
    if (!ZoneSuits(At<int32_t>(profile, 0x0), zone)) {
        *reason = 0;
        return 0;
    }
    void* const lot = Virtual<void*>(At<void*>(self, local::kLots), 0x18, x(), z(), 1u);
    if (!lot) {
        *reason = 3;
        return 0;
    }
    if (!At<uint8_t>(self, local::kIgnorePower) && !Virtual<uint8_t>(At<void*>(self, local::kPower), 0x4C, x(), z())) {
        *reason = 5;
        return 0;
    }
    if (At<uint8_t>(profile, 0x10) == 0 && At<uint8_t>(profile, 0x11) == 0) {
        const uint32_t wealth = Virtual<uint32_t>(At<void*>(self, local::kWealth), 0x14, x(), z());
        const int32_t purpose = At<int32_t>(profile, 0x0);
        if (purpose != 7 && purpose != 8 && purpose != 9 && wealth != At<uint32_t>(profile, 0x8)) {
            *reason = 2;
            return 0;
        }
    } else {
        const int32_t value = Virtual<uint8_t>(At<void*>(self, local::kWealth), 0x18, x(), z());
        if (value < At<uint8_t>(profile, 0x10) || value > At<uint8_t>(profile, 0x11)) {
            *reason = 1;
            return 0;
        }
    }
    if (Virtual<int32_t>(lot, 0x84) != 1) {
        *reason = 4;
        return 0;
    }
    return 1;
}

// Slot 47 (0x00695570): builds a construction profile's building at its
// cell (slot 6); what slot 6 returns.
void* __fastcall PlopBuilding(void* self, void*, char* profile)
{
    static bool logged = false;
    FirstCall("PlopBuilding", logged);
    return Virtual<void*>(self, 0x18, At<uint32_t>(profile, 0x14), At<uint32_t>(profile, 0x1C),
                          At<uint32_t>(profile, 0x20), At<uint32_t>(profile, 0x18));
}

// --- Installation ------------------------------------------------------------

struct Slot {
    std::uintptr_t vtable;
    int index;
    std::uintptr_t original;
    void* ours;
};

#define OPENSC4_SLOT(index, original, fn)                                                                    \
    {addr::cSC4BuildingDevelopmentSimulator_vftable, index, original, reinterpret_cast<void*>(&fn)}

const Slot kSlots[] = {
    OPENSC4_SLOT(6, 0x006956A0, AddBuildingAtCell),
    OPENSC4_SLOT(7, 0x00698DE0, AddBuildingAt),
    OPENSC4_SLOT(8, 0x00695740, AddOccupant),
    OPENSC4_SLOT(22, 0x00698120, FoundationKeyOfType),
    OPENSC4_SLOT(23, 0x00695AF0, AddFoundationOccupant),
    OPENSC4_SLOT(24, 0x00695740, AddOccupant),
    OPENSC4_SLOT(26, 0x006982B0, LoadFoundationProperties),
    OPENSC4_SLOT(29, 0x006983C0, LoadLotRetainingWallProperties),
    OPENSC4_SLOT(46, 0x00695430, IsSuitableForConstruction),
    OPENSC4_SLOT(47, 0x00695570, PlopBuilding),
};

#undef OPENSC4_SLOT

template <std::size_t N>
bool SwapSlots(const Slot (&slots)[N], const char* what)
{
    if (!game::IsBuild641())
        return false;
    for (const Slot& slot : slots) {
        const auto* const vtable = reinterpret_cast<const std::uintptr_t*>(slot.vtable);
        if (vtable[slot.index] != slot.original) {
            log::Write("%s: slot %d of %08X holds %08X, expected %08X; not installing", what, slot.index,
                       static_cast<unsigned>(slot.vtable), static_cast<unsigned>(vtable[slot.index]),
                       static_cast<unsigned>(slot.original));
            return false;
        }
    }
    for (const Slot& slot : slots) {
        auto* const entry = reinterpret_cast<std::uintptr_t*>(slot.vtable) + slot.index;
        DWORD old = 0;
        if (!VirtualProtect(entry, sizeof(*entry), PAGE_READWRITE, &old))
            return false;
        *entry = reinterpret_cast<std::uintptr_t>(slot.ours);
        VirtualProtect(entry, sizeof(*entry), old, &old);
    }
    log::Write("%s: %u slots are ours", what, static_cast<unsigned>(N));
    return true;
}

bool Install(registry::Mode) { return SwapSlots(kSlots, "development sim occupants"); }

const bool g_registered = registry::Register(
    {"simulators.development.occupants", "simulators/development", registry::Kind::Vtable,
     registry::Phase::PostAppInit, addr::cSC4BuildingDevelopmentSimulator_vftable,
     "Our cISC4BuildingDevelopmentSimulator occupant and construction functions: adding buildings and "
     "foundations, foundation keys, foundation and retaining wall properties, the construction check, "
     "plopping (docs/development.md)",
     false, Install});

} // namespace
} // namespace opensc4::development_sim
