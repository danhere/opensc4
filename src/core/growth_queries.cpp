// Our cSC4GrowthDeveloper's queries (build 641; docs/growth.md): the
// construction info, the map update state, the decline list, the caps and
// thresholds, the style percentages, the desirability estimate of a cell and
// the subjective thresholds. Every function is a replacement on the original
// object (slots of the primary vtable 0x00AB1488).
//
// Fields used here (offsets from the object's start), to merge into
// growth_sim.h. Properties are the developer exemplar's, read by 0x006B78B0
// (called from Init):
//   0x34 kDemand          the source demand (property 0x87CD6301); shared
//                         slot 18 (0x00A6CAB0) returns it
//   0x38 kPurpose         uint32 PurposeType (byte property 0x87CD6332);
//                         shared slot 7 (0x0080BC00); GetConstructionInfo
//                         maps 5 (agriculture) to developer type 7 and 6..8
//                         to 8. (growth_sim.h's kType is this field.)
//   0x3C kWealth          uint32 (byte property 0x87CD6330); shared slot 8
//   0x40 kConstruction40  copied to construction info +4; only the
//                         constructor (0x006B7360) writes it
//   0x45, 0x46            uint8 land value range min/max (the first two
//                         bytes of property 0x87CD6333); info +0x10/+0x11
//   0x47 kUseDesirability2  bool property 0x87CD633F: EstimateDesirability
//                         takes the 0x00B43D88 grid of slot 7 instead of 8
//   0x48, 0x4C, 0x5C, 0x60, 0x64, 0x68, 0x6C, 0x70, 0x74, 0x78, 0x7C, 0x84,
//   0x88, 0x8C            SC4Curve* (null without the property): properties
//                         0x87CD6340, 6398, 6341, 6343, 6344, 6345, 6391,
//                         6392, 6348, 6349, 634A, 6346, 6399, 6397. (+0x80,
//                         property 0x87CD6347, is loaded the same way but not
//                         read here.)
//   0x50                  SC4Curve*[4] (properties 0x87CD634B..634E), one per
//                         pollution simulator grid index 0..3
//   0x90 kDesirabilityBase  int32 (property 0x87CD6357): the estimate's start
//   0x94 kDesirabilityThreshold  int32 (property 0x87CD6355); slot 23
//   0xAC..0xAF            uint8 power, water, fire coverage and park caps
//                         (properties 0x87CD6373, 6370, 6371, 6372); slots
//                         19-22
//   0xB0                  std::map<int32 density, float> abandonment
//                         thresholds (property 0x87CD6380 region); slot 24
//   0xBC kMonopolyUse     shared slot 25 (0x008216D0)
//   0xC0                  the style percentages (map); slot 26 returns its
//                         address
//   0xCC kSubType         shared slot 6 (0x00472E50)
//   0xD0 kZoneKind        TargetZoneType 1-3 (byte property 0x87CD6300);
//                         shared slot 5 (0x00441080)
//   0xD4 kMapState        the map update's state (0 idle; slot 27 sets 1)
//   0xEC                  the growth list (shared slot 11, 0x00798040)
//   0xF8                  the decline list (slot 12)
//   0x108                 the total capacity (shared slot 17, 0x004236F0)
//   0x114, 0x11C, 0x118, 0x120  the grids of shared slots 13, 14, 15, 16
//   0x124, 0x130          vectors of int32 (begin, end, capacity): minimum
//                         and maximum subjective thresholds (properties
//                         0xCAB1CE3A, 0xCAB1CE3C)
#include "core/game.h"
#include "core/game_access.h"
#include "core/growth_sim.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <iterator>

namespace opensc4::growth_sim {
namespace {

namespace local {
constexpr std::size_t kPurpose = 0x38;
constexpr std::size_t kWealth = 0x3C;
constexpr std::size_t kConstruction40 = 0x40;
constexpr std::size_t kLandValueMin = 0x45;
constexpr std::size_t kLandValueMax = 0x46;
constexpr std::size_t kUseDesirability2 = 0x47;
constexpr std::size_t kPollutionCurves = 0x50; // SC4Curve*[4]
constexpr std::size_t kDesirabilityBase = 0x90;
constexpr std::size_t kDesirabilityThreshold = 0x94;
constexpr std::size_t kPowerCap = 0xAC;
constexpr std::size_t kWaterCap = 0xAD;
constexpr std::size_t kFireCap = 0xAE;
constexpr std::size_t kParkCap = 0xAF;
constexpr std::size_t kAbandonment = 0xB0;
constexpr std::size_t kStyles = 0xC0;
constexpr std::size_t kDeclineList = 0xF8;
constexpr std::size_t kMinThresholds = 0x124;
constexpr std::size_t kMaxThresholds = 0x130;
} // namespace local

using EvaluateCurveFn = double(__thiscall*)(const void* curve, float x); // ST0, unrounded
using IndexFn = float*(__thiscall*)(void* map, const int32_t* key);

// The truncating float-to-int conversion (_ftol, 0x009EEF04): the low dword
// of the int64.
int32_t Ftol(double value) { return static_cast<int32_t>(static_cast<int64_t>(value)); }

// One term of the estimate: a curve at a value, truncated.
int32_t Term(const void* curve, float value)
{
    return Ftol(reinterpret_cast<EvaluateCurveFn>(addr::SC4Curve_Evaluate)(curve, value));
}

// --- The interface -----------------------------------------------------------

// Slot 9 (0x006B38E0): fills a construction profile: the developer type by
// zone kind (residential 1, commercial 4, industrial 7 for agriculture, 8
// for processing, manufacturing and high tech; another industrial purpose
// leaves +0 as it was; another zone kind 0), +4 the field +0x40, +8 wealth,
// +0xC purpose, +0x10/+0x11 the land value range. Returns the profile (EAX).
uint32_t* __fastcall GetConstructionInfo(void* self, void*, uint32_t* info)
{
    static bool logged = false;
    FirstCall("GetConstructionInfo", logged);
    switch (At<int32_t>(self, field::kZoneKind)) {
    case 1:
        info[0] = 1;
        break;
    case 2:
        info[0] = 4;
        break;
    case 3:
        switch (At<int32_t>(self, local::kPurpose)) {
        case 5:
            info[0] = 7;
            break;
        case 6:
        case 7:
        case 8:
            info[0] = 8;
            break;
        default:
            break;
        }
        break;
    default:
        info[0] = 0;
        break;
    }
    info[1] = At<uint32_t>(self, local::kConstruction40);
    info[2] = At<uint32_t>(self, local::kWealth);
    info[3] = At<uint32_t>(self, local::kPurpose);
    auto* const bytes = reinterpret_cast<uint8_t*>(info);
    bytes[0x10] = At<uint8_t>(self, local::kLandValueMin);
    bytes[0x11] = At<uint8_t>(self, local::kLandValueMax);
    return info;
}

// Slot 10 (0x006B7530): the lists are valid while the map update's state
// is below 0x11 (signed).
uint32_t __fastcall GrowthAndDeclineListsAreValid(void* self, void*)
{
    static bool logged = false;
    FirstCall("GrowthAndDeclineListsAreValid", logged);
    return At<int32_t>(self, field::kMapState) < 0x11 ? 1u : 0u;
}

// Slot 12 (0x006B7540): the decline list's address.
void* __fastcall GetDeclineList(void* self, void*)
{
    static bool logged = false;
    FirstCall("GetDeclineList", logged);
    return static_cast<char*>(self) + local::kDeclineList;
}

// Slots 19-22 (0x006B7550, 0x006B7560, 0x006B7570, 0x006B7580): the caps.
// The game loads only AL; the rest of EAX is what its caller had.
uint8_t __fastcall GetPowerSupplyCap(void* self, void*)
{
    static bool logged = false;
    FirstCall("GetPowerSupplyCap", logged);
    return At<uint8_t>(self, local::kPowerCap);
}

uint8_t __fastcall GetWaterSupplyCap(void* self, void*)
{
    static bool logged = false;
    FirstCall("GetWaterSupplyCap", logged);
    return At<uint8_t>(self, local::kWaterCap);
}

uint8_t __fastcall GetFireCoverageCap(void* self, void*)
{
    static bool logged = false;
    FirstCall("GetFireCoverageCap", logged);
    return At<uint8_t>(self, local::kFireCap);
}

uint8_t __fastcall GetParkCap(void* self, void*)
{
    static bool logged = false;
    FirstCall("GetParkCap", logged);
    return At<uint8_t>(self, local::kParkCap);
}

// Slot 23 (0x006B75A0): the desirability threshold.
int32_t __fastcall GetDesirabilityThreshold(void* self, void*)
{
    static bool logged = false;
    FirstCall("GetDesirabilityThreshold", logged);
    return At<int32_t>(self, local::kDesirabilityThreshold);
}

// Slot 24 (0x006B7620): the abandonment threshold of a density, through the
// game's operator[] on the map at +0xB0 (inserts 0 when missing). Returned
// in ST0 (a float load: exact).
float __fastcall GetAbandonmentThreshold(void* self, void*, int32_t density)
{
    static bool logged = false;
    FirstCall("GetAbandonmentThreshold", logged);
    void* const map = static_cast<char*>(self) + local::kAbandonment;
    return *reinterpret_cast<IndexFn>(addr::SC4Sint32FloatMap_Index)(map, &density);
}

// Slot 26 (0x006B7590): the style percentages' address.
void* __fastcall GetStylePercentages(void* self, void*)
{
    static bool logged = false;
    FirstCall("GetStylePercentages", logged);
    return static_cast<char*>(self) + local::kStyles;
}

// Slot 27 (0x006B75B0): starts the map update when idle (state 1). Returns
// the state it found (EAX).
int32_t __fastcall StartMapUpdate(void* self, void*)
{
    static bool logged = false;
    FirstCall("StartMapUpdate", logged);
    const int32_t state = At<int32_t>(self, field::kMapState);
    if (state == 0)
        At<int32_t>(self, field::kMapState) = 1;
    return state;
}

// Slot 28 (0x006B75D0): the map update is finished when idle.
uint32_t __fastcall IsMapUpdateFinished(void* self, void*)
{
    static bool logged = false;
    FirstCall("IsMapUpdateFinished", logged);
    return At<int32_t>(self, field::kMapState) == 0 ? 1u : 0u;
}

// Slot 29 (0x006B4DF0): the desirability a cell would have: first the
// +0x114 grid's slot 11 (+0x2C; a getter, 0x00808620 returns its +0x1C,
// result unused), then the base plus,
// for every curve that is set, the curve at a grid value of the cell
// (each grid's slot 7, +0x1C (x, z)), truncated. The grids, in the game's
// order:
//   +0x48  0x00B43D88 slot 7 (+0x1C) if +0x47 is set, else slot 8 (+0x20); byte
//   +0x4C  0x00B43D88 slot 9 (+0x24); float (ST0, stored as float)
//   +0x60, +0x64, +0x68  0x00B43DA4 slot 5 (+0x14) with 1, 2, 3; byte
//   +0x6C, +0x70  0x00B43D6C slot 5 with 0x3320, 0x3330; byte
//   +0x5C  0x00B43D60 slot 14 (+0x38); byte
//   +0x50[i], i = 0..3  0x00B43D9C (pollution) slot 5 with i; uint16
//   +0x74, +0x78  0x00B43DA4 slots 39 (+0x9C), 40 (+0xA0); float
//   +0x7C  0x00B43D70 slot 5, no argument; byte
//   +0x84  0x00B43DA8 slot 9 (+0x24); byte
//   +0x88, +0x8C  0x00B43D60 slots 15 (+0x3C), 13 (+0x34); byte
// The game returns the sum's low 16 bits in AX; the upper half of EAX is
// the last term's (or 0 when the +0x8C curve is not set: EAX then holds
// that null field).
uint32_t __fastcall EstimateDesirability(void* self, void*, int32_t x, int32_t z)
{
    static bool logged = false;
    FirstCall("EstimateDesirability", logged);
    Virtual<uint32_t>(At<void*>(self, field::kGrid114), 0x2C);
    int32_t sum = At<int32_t>(self, local::kDesirabilityBase);
    uint32_t eax = 0;
    auto add = [&](const void* curve, float value) {
        const int32_t term = Term(curve, value);
        eax = static_cast<uint32_t>(term);
        sum += term;
    };

    if (At<void*>(self, 0x48)) {
        void* const service = Global<void*>(addr::SC4_Service43D88);
        void* const grid = At<uint8_t>(self, local::kUseDesirability2) ? Virtual<void*>(service, 0x1C)
                                                                       : Virtual<void*>(service, 0x20);
        const uint8_t v = Virtual<uint8_t>(grid, 0x1C, x, z);
        add(At<void*>(self, 0x48), static_cast<float>(v));
    }
    if (At<void*>(self, 0x4C)) {
        void* const grid = Virtual<void*>(Global<void*>(addr::SC4_Service43D88), 0x24);
        // ST0 stored straight to a float (one rounding, as the game's fstp).
        const float v = Virtual<float>(grid, 0x1C, x, z);
        add(At<void*>(self, 0x4C), v);
    }
    void* const civic = Global<void*>(addr::SC4_Service43DA4);
    for (int32_t i = 1; i <= 3; ++i) {
        const std::size_t offset = 0x5C + 4 * static_cast<std::size_t>(i); // 0x60, 0x64, 0x68
        if (At<void*>(self, offset)) {
            void* const grid = Virtual<void*>(civic, 0x14, i);
            const uint8_t v = Virtual<uint8_t>(grid, 0x1C, x, z);
            add(At<void*>(self, offset), static_cast<float>(v));
        }
    }
    void* const service6C = Global<void*>(addr::SC4_Service43D6C);
    if (At<void*>(self, 0x6C)) {
        void* const grid = Virtual<void*>(service6C, 0x14, 0x3320u);
        const uint8_t v = Virtual<uint8_t>(grid, 0x1C, x, z);
        add(At<void*>(self, 0x6C), static_cast<float>(v));
    }
    if (At<void*>(self, 0x70)) {
        void* const grid = Virtual<void*>(service6C, 0x14, 0x3330u);
        const uint8_t v = Virtual<uint8_t>(grid, 0x1C, x, z);
        add(At<void*>(self, 0x70), static_cast<float>(v));
    }
    if (At<void*>(self, 0x5C)) {
        void* const grid = Virtual<void*>(Global<void*>(addr::SC4_Service43D60), 0x38);
        const uint8_t v = Virtual<uint8_t>(grid, 0x1C, x, z);
        add(At<void*>(self, 0x5C), static_cast<float>(v));
    }
    void* const pollution = Global<void*>(addr::SC4_PollutionSimulator);
    for (int32_t i = 0; i < 4; ++i) {
        if (!At<void**>(self, local::kPollutionCurves)[i])
            continue;
        void* const grid = Virtual<void*>(pollution, 0x14, i);
        const uint16_t v = Virtual<uint16_t>(grid, 0x1C, x, z);
        add(At<void**>(self, local::kPollutionCurves)[i], static_cast<float>(v));
    }
    if (At<void*>(self, 0x74)) {
        void* const grid = Virtual<void*>(Global<void*>(addr::SC4_Service43DA4), 0x9C);
        const float v = Virtual<float>(grid, 0x1C, x, z);
        add(At<void*>(self, 0x74), v);
    }
    if (At<void*>(self, 0x78)) {
        void* const grid = Virtual<void*>(Global<void*>(addr::SC4_Service43DA4), 0xA0);
        const float v = Virtual<float>(grid, 0x1C, x, z);
        add(At<void*>(self, 0x78), v);
    }
    if (At<void*>(self, 0x7C)) {
        void* const grid = Virtual<void*>(Global<void*>(addr::SC4_Service43D70), 0x14);
        const uint8_t v = Virtual<uint8_t>(grid, 0x1C, x, z);
        add(At<void*>(self, 0x7C), static_cast<float>(v));
    }
    if (At<void*>(self, 0x84)) {
        void* const grid = Virtual<void*>(Global<void*>(addr::SC4_Service43DA8), 0x24);
        const uint8_t v = Virtual<uint8_t>(grid, 0x1C, x, z);
        add(At<void*>(self, 0x84), static_cast<float>(v));
    }
    if (At<void*>(self, 0x88)) {
        void* const grid = Virtual<void*>(Global<void*>(addr::SC4_Service43D60), 0x3C);
        const uint8_t v = Virtual<uint8_t>(grid, 0x1C, x, z);
        add(At<void*>(self, 0x88), static_cast<float>(v));
    }
    eax = 0; // the +0x8C test leaves the (null) field in EAX when it skips
    if (At<void*>(self, 0x8C)) {
        void* const grid = Virtual<void*>(Global<void*>(addr::SC4_Service43D60), 0x34);
        const uint8_t v = Virtual<uint8_t>(grid, 0x1C, x, z);
        add(At<void*>(self, 0x8C), static_cast<float>(v));
    }
    return (eax & 0xFFFF0000u) | (static_cast<uint32_t>(sum) & 0xFFFFu);
}

// Slot 30 (0x006B5410): the subjective thresholds of an index: the minimum
// from the vector at +0x124 (-4 when the index is outside it), the maximum
// from the vector at +0x130 (20 when outside). Unsigned bounds checks after
// a signed index >= 0 test. EAX: the maximum's pointer when found, else the
// index.
uint32_t __fastcall GetSubjectiveThresholds(void* self, void*, int32_t index, int32_t* min, int32_t* max)
{
    static bool logged = false;
    FirstCall("GetSubjectiveThresholds", logged);
    auto inside = [&](std::size_t vector) {
        const auto begin = At<uintptr_t>(self, vector);
        const auto count = static_cast<uint32_t>(static_cast<int32_t>(At<uintptr_t>(self, vector + 4) - begin) >> 2);
        return index >= 0 && static_cast<uint32_t>(index) < count;
    };
    *min = inside(local::kMinThresholds) ? At<int32_t*>(self, local::kMinThresholds)[index] : -4;
    if (inside(local::kMaxThresholds)) {
        *max = At<int32_t*>(self, local::kMaxThresholds)[index];
        return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(max));
    }
    *max = 20;
    return static_cast<uint32_t>(index);
}

// --- Installation ------------------------------------------------------------

struct Slot {
    int index;
    std::uintptr_t original;
    void* ours;
};

#define OPENSC4_SLOT(index, original, fn) {index, original, reinterpret_cast<void*>(&fn)}

const Slot kSlots[] = {
    OPENSC4_SLOT(9, 0x006B38E0, GetConstructionInfo),
    OPENSC4_SLOT(10, 0x006B7530, GrowthAndDeclineListsAreValid),
    OPENSC4_SLOT(12, 0x006B7540, GetDeclineList),
    OPENSC4_SLOT(19, 0x006B7550, GetPowerSupplyCap),
    OPENSC4_SLOT(20, 0x006B7560, GetWaterSupplyCap),
    OPENSC4_SLOT(21, 0x006B7570, GetFireCoverageCap),
    OPENSC4_SLOT(22, 0x006B7580, GetParkCap),
    OPENSC4_SLOT(23, 0x006B75A0, GetDesirabilityThreshold),
    OPENSC4_SLOT(24, 0x006B7620, GetAbandonmentThreshold),
    OPENSC4_SLOT(26, 0x006B7590, GetStylePercentages),
    OPENSC4_SLOT(27, 0x006B75B0, StartMapUpdate),
    OPENSC4_SLOT(28, 0x006B75D0, IsMapUpdateFinished),
    OPENSC4_SLOT(29, 0x006B4DF0, EstimateDesirability),
    OPENSC4_SLOT(30, 0x006B5410, GetSubjectiveThresholds),
};

#undef OPENSC4_SLOT

// Checks that every slot still holds the original, then swaps them all.
bool SwapSlots()
{
    if (!game::IsBuild641())
        return false;
    auto* const vtable = reinterpret_cast<std::uintptr_t*>(addr::cSC4GrowthDeveloper_vftable);
    for (const Slot& slot : kSlots) {
        if (vtable[slot.index] != slot.original) {
            log::Write("growth developer queries: slot %d of %08X holds %08X, expected %08X; not installing",
                       slot.index, static_cast<unsigned>(addr::cSC4GrowthDeveloper_vftable),
                       static_cast<unsigned>(vtable[slot.index]), static_cast<unsigned>(slot.original));
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
    log::Write("growth developer queries: %u slots are ours", static_cast<unsigned>(std::size(kSlots)));
    return true;
}

bool Install(registry::Mode) { return SwapSlots(); }

const bool g_registered = registry::Register(
    {"simulators.growth.queries", "simulators/growth", registry::Kind::Vtable, registry::Phase::PostAppInit,
     addr::cSC4GrowthDeveloper_vftable,
     "Our cSC4GrowthDeveloper queries: construction info, map update state, decline list, caps, thresholds, "
     "styles, desirability estimate, subjective thresholds (docs/growth.md)",
     false, Install});

} // namespace
} // namespace opensc4::growth_sim
