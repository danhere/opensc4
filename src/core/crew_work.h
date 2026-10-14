// Our cSC4NetworkConstructionCrew, part C2b (build 641): the planning and
// running of the jobs (crew_work.cpp). The class map, the job record and the
// fields are in crew_sim.h (part C1); what this part adds is here.
//
// The functions of the part (each a detour at the game's address, off by
// default; OPENSC4_CREW_SKIP leaves any of them the game's):
//   simulators.crew.running   Tick 0x00607C30, BeginJob 0x00607890 and
//                             NextJobPiece 0x00607AD0
//   simulators.crew.planning  AddSegment 0x00606B50, PlanJobs 0x00607F30 and
//                             Start 0x00608770
// SpawnPedestrians 0x006072E0 stays the game's (crew_work.cpp says why).
//
// The crew's frame time and stopwatches. The game's Tick is hooked by
// determinism.sim_timers (sim_timers.cpp) to get the simulation time of the
// last frame; two detours cannot share 0x00607C30, so with our Tick there
// the hook stays off and our Tick asks sim_timers::CrewFrameMicroseconds
// itself (which gives the frame message's value back when the switch is
// off). The running group installs at PreAppInit, before sim_timers
// (PostAppInit) asks TickIsOurs. In the other order (a hook already at the
// entry when the running group installs) our Tick stays out, logs it, and
// the hook keeps the game's Tick. Our functions call the stopwatch through
// sim_timers::CrewStopwatchStart / CrewStopwatchElapsed (sim_timers.h), so
// that their calls from core.dll are served as the crew's range is.
#pragma once

#include "core/crew_sim.h"

#include <cstddef>
#include <cstdint>

namespace opensc4::crew_sim {

// True once our Tick sits at 0x00607C30 (determinism.sim_timers then does
// not hook it).
bool TickIsOurs();

// A network piece's slot 0x30, GetNetworkFlag (none; the flags word): the
// job's messages carry it as their data 3 (src/core/netocc_sim.h).
inline constexpr std::size_t kPieceGetNetworkFlag = 0x30;

// The 12-byte segment record (the crew's vector +0x30, the tool's +0x54):
// the indices of its first and last cell in the 8-byte cell records (the
// crew's +0x3C, the tool's +0x60; each x then z), and a third word compared
// as is.
namespace segment {
inline constexpr std::size_t kSize = 0xC, kFirst = 0x0, kLast = 0x4, kWord8 = 0x8;
} // namespace segment
inline constexpr std::size_t kCellRecordSize = 0x8;

} // namespace opensc4::crew_sim
