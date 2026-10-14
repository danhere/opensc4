// Our cSC4NetworkConstructionCrew, part C2a (build 641): the workers, the
// dust effect and the job's End (src/core/crew_workers.cpp). The class map,
// the job record and the worker record are in src/core/crew_sim.h; this
// header adds the slots of the other classes the workers call, with the
// argument counts each callee's own `ret N` gives (va-tools vt.py).
#pragma once

#include "core/crew_sim.h"

#include <cstddef>
#include <cstdint>

namespace opensc4::crew_sim {

// The slot names are ours, from what the crew passes and reads; the counts
// are the callees' own.
//
// The worker vehicle, an automaton the vehicle manager makes (the same
// counts in every automaton vtable that has these slots, e.g. 0x00A94FB0,
// 0x00A9C068, 0x00A9D488).
namespace vehicle_slot {
inline constexpr std::size_t kGetPosition = 0x1C;  // one: float3* out (0x00505520)
inline constexpr std::size_t kSetDirection = 0x3C; // one: float3* (0x00505540)
inline constexpr std::size_t kPlace = 0x40;        // two: float3* position, float3* direction (0x005080C0)
inline constexpr std::size_t kSetMode = 0x78;      // two: 2, 0 (0x004FAD20)
inline constexpr std::size_t kSetSpeed = 0x84;     // two: a float (0 or 1.0f), 0 (0x0050B360)
inline constexpr std::size_t kLeave = 0xA0;        // one: the crew (0x0050BB90)
inline constexpr std::size_t kFollowPath = 0x210;  // four: the piece, the path, 0, 1
inline constexpr std::size_t kMove = 0x214;        // six: the step (float), float3*, float3*, 0, 0, 0 (0x005504E0)
inline constexpr std::size_t kStartPath = 0x22C;   // one: 0 (0x00623B80)
} // namespace vehicle_slot

// The managers of the city (SC4::City 0x00B43CEC): slot 0xAC the vehicle
// manager (cSC4City::GetVehicleManager, +0x1D0), slot 0xB0 the pedestrian
// manager (+0x1D4). Both vtables (0x00A9D1E0, 0x00A9A470) share these.
inline constexpr std::size_t kCityVehicleManager = 0xAC, kCityPedestrianManager = 0xB0;
namespace automata_slot {
inline constexpr std::size_t kRemove = 0x20; // two: the automaton, 0 (0x0054D690)
inline constexpr std::size_t kMake = 0x3C;   // seven (0x00505370): 0x4002, float3*, 0x3B000, key*, 0, 0x32, 0
} // namespace automata_slot
// The model key MakeWorkerVehicle asks for: these two words and the worker's
// value (+0x4, the crew's +0x20 or +0x24).
inline constexpr uint32_t kVehicleKeyType = 0x5AD0E817, kVehicleKeyGroup = 0xBADB57F1;

// The path information of a piece (the piece's slot 0xC4), vtable
// 0x00A99E70.
namespace path_slot {
inline constexpr std::size_t kFindPath = 0xC;  // two: the path ID, 0; the path or null (0x0053DD30)
inline constexpr std::size_t kHasPath = 0x10;  // five (0x0053CFF0)
inline constexpr std::size_t kChoose = 0x24;   // six: a, from, to, the generator, uint32* out, 0 (0x0053FB80)
inline constexpr std::size_t kEndPoint = 0x58; // two: the path, float3* out (0x0053D890)
inline constexpr std::size_t kMark = 0x7C;     // four: 0xFF, 1, 5, 1 (0x005425C0)
inline constexpr std::size_t kFirst = 0x94;    // three: a, from, to; the path ID (0x0053F100)
} // namespace path_slot

// A network piece's slots the workers call besides crew_sim.h's.
inline constexpr std::size_t kPieceGetPathInfo = 0xC4, kPieceGetId = 0x5C;
// An occupant's slots (src/core/occbase_sim.h).
inline constexpr std::size_t kOccupantGetPosition = 0x20, kOccupantGetBounds = 0x28; // one; two

// The effects manager (SC4::Service43D1C, vtable 0x00A9F264): slot 7 makes
// an effect by name (two: the name, the effect out); the visual effect
// (vtable 0x00AA1170): slot 3 Start (one), slot 4 Stop (one), slot 8 the
// transform (one: the 0x38-byte block below).
inline constexpr std::size_t kEffectsMake = 0x1C;
inline constexpr std::size_t kEffectStart = 0xC, kEffectStop = 0x10, kEffectSetTransform = 0x20;

// The block MakeDustEffect hands the effect's slot 8, as the game fills it
// on its stack (0x00605A1C to 0x00605A14): two bytes set, two left (the
// game leaves stack bytes there; ours are zero), a 3 by 3 identity, the
// position, a scale of 1.
struct EffectTransform {
    uint8_t flag0;
    uint8_t flag1;
    uint8_t unset[2];
    float matrix[9];
    float position[3];
    float scale;
};
static_assert(sizeof(EffectTransform) == 0x38);

// The terrain (SC4::Terrain 0x00B43CF4, vtable 0x00AB3D18) slot 34: two (the
// cell x, z); PlaceWorker's speed is 0 when it answers true.
inline constexpr std::size_t kTerrainSlot88 = 0x88;

} // namespace opensc4::crew_sim
