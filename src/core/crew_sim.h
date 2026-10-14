// Shared between the parts of our cSC4NetworkConstructionCrew (build 641):
// the class map, the job record, the vtable slot by slot and the fields. A
// crew is what a network tool leaves behind when it builds: it holds the
// new pieces of one placement, hands them out to jobs (each with two worker
// vehicles, a dust effect and some pedestrians), and makes each piece usable
// when its job has spent the piece's time on it. It has no class ID, no
// factory and no save record: a save holds no crew.
//
// Written in parts: crew_life.cpp (C1, state and life: the constructor, the
// destructor, Release, Init, Shutdown, DoMessage, OnOccupantRemoved, the
// static pair, PostMessage, FinishPiece and FinishPieces, the two work
// property counters, and the job record's and the piece list's helpers);
// the second part (C2, the work) is in crew_workers.cpp (the workers, the
// dust effect, the job's End) and crew_work.cpp (AddSegment, PlanJobs,
// Start, the job's Begin and NextPiece, Tick; the pedestrians stay the
// game's). docs/netlot.md has all of it.
//
// === cSC4NetworkConstructionCrew ==========================================
// 0x54 bytes, no class ID. Made only by the network tool's PlaceAllSegments
// (0x0063BDC0): operator new 0x54 (0x0063BE09) and the constructor 0x006070C0
// (0x0063BE17), for the network types 0 to 3, 6 and 8 to 12 when the build
// argument is set (types 4 pipe, 5 power line and 7 subway get none: the
// index table 0x0063BF20 and the jump table 0x0063BF18). The tool then
// calls AddRef, Init with six values of its tuning table 0x00B452C8 (one
// record of 0x114 bytes a network type: +0x1C, +0xC8, +0xCC, +0xD0, +0xD4,
// +0xD8), AddSegment 0x00606B50 per segment (from 0x0063B830), Start
// 0x00608770 and Release. Init registers the crew with the network manager
// (its slot 16, which keeps a reference); Shutdown unregisters it (the
// manager's slot 17 releases that reference, which may destroy the crew).
// Code 0x00605490 to 0x00608C4B; 0x00606870 in that range is the shared
// SC4PairVector::Clear, not the crew's.
//
// --- +0x0: cIGZMessageTarget2, vtable 0x00AA74AC, 8 slots -----------------
// Argument counts are each function's own `ret N`.
//    0 QueryInterface 0x005C3E30 (SHARED code: IID 0x452294AA only, not 1)
//    1 AddRef 0x004F7BA0 (SHARED: ++[+0x10])
//    2 Release 0x00608C20
//    3 DoMessage(message) 0x00607ED0
//    4 Init(count, seconds, scale, a, b, c) 0x00607190 (ret 0x18; the two
//      floats on the stack as their 32 bits)
//    5 Shutdown 0x00607920
//    6 StaticInit 0x00605620 (the byte 0x00B43E60 set; Init calls it when
//      the byte is clear)
//    7 StaticShutdown 0x00605640 (the byte cleared; no caller found)
// Each of slots 2 to 7 is only in this vtable (a scan of the exe for the
// addresses finds them at 0x00AA74B4 to 0x00AA74C8 and nowhere else).
//
// Fields (offsets from the object's start). Evidence: the constructor
// 0x006070C0, Init, Shutdown, ResetJob 0x00605A40.
//   +0x04 uint32 the milliseconds a job spends on a piece: Init's ftol of
//         its second argument (float seconds) times 1000.0f (0x00A8FAF0)
//   +0x08 float  Init's third argument times 1e-6f (0x00A86510)
//   +0x0C uint32 Init's first argument (the tool's +0x1C; Start reads it)
//   +0x10 uint32 the reference count (AddRef 0x004F7BA0, Release)
//   +0x14 uint8  initialised (Init sets it, Shutdown clears it)
//   +0x15 uint8  in the frame service (0x86AD10EC, code 0xC0; Start joins,
//         Shutdown leaves)
//   +0x18 the sound service (0x00B43CB8), referenced, or null: Init stores
//         it unless the plugin's byte or game.quiet.network says no; the
//         job's start and end messages (PostMessage) are sent only with it
//   +0x1C uint32 cleared by the constructor, read by nothing of C1
//   +0x20, +0x24 Init's fourth and fifth arguments: ResetJob copies them to
//         each job's two workers (+0x40, +0x60)
//   +0x28 Init's sixth argument
//   +0x2C std::list of pieces (a pointer to the head node; nodes of 0xC
//         bytes: next, previous, the piece; each piece referenced by the
//         list)
//   +0x30 vector of 12-byte segment records (begin, end, capacity)
//   +0x3C vector of 8-byte records (begin, end, capacity)
//   +0x48 vector of jobs (0x80 bytes each; begin, end, capacity)
//
// === The job (0x80 bytes, in the vector at +0x48) =========================
// ConstructJob 0x00605CC0, AssignJob 0x006068A0 (operator=), CopyJob
// 0x00606980 (copy constructor), CopyJobs 0x00606A90 (copy of a range by
// AssignJob), DestroyJobs 0x00606AE0 (destruction of a range), ResetJob
// 0x00605A40 (from the crew's values).
//   +0x00 uint32 milliseconds per piece (the crew's +0x4)
//   +0x04 int32  state (-1 ended)
//   +0x08 the current piece (referenced)
//   +0x0C the dust effect
//   +0x10 vector of pedestrians (begin, end, capacity)
//   +0x1C std::list of the job's pieces (a pointer to the head node)
//   +0x20 the stopwatch (SC4Stopwatch, 0x18 bytes, mode 4)
//   +0x38 uint8
//   +0x3C worker A, +0x5C worker B (0x20 bytes each): +0x0 uint8, +0x4 the
//         crew's +0x20 (A) or +0x24 (B), +0x8 the vehicle, +0xC three
//         floats (ConstructJob copies the word at 0x00B43E64 into each),
//         +0x18 uint8 arrived, +0x19 uint8, +0x1C the piece it works on
//
// === The crew's clocks ====================================================
// determinism.sim_timers (src/core/sim_timers.cpp) serves every stopwatch
// call whose return address is in the crew's code (0x00605490 to
// 0x00608C50) from the simulation counter, and hooks Tick 0x00607C30. No
// function of C1 calls a stopwatch method that reads the clock: ConstructJob
// calls SC4Stopwatch::Construct 0x0088FEDF (mode and zeros) and DestroyJobs
// SC4Stopwatch::Destruct 0x00A6D837 (a bare `ret`), neither hooked by
// sim_timers; AssignJob and CopyJob copy the stopwatch's 0x18 bytes inline.
// Shutdown calls the job's End 0x00606750 at its address, so End's Stop
// (0x00606855) still returns into the crew's range, and DoMessage calls Tick
// at its address, where sim_timers' hook sits, or ours with
// simulators.crew.running (then sim_timers leaves Tick alone). Our C2 code
// calls the stopwatch through sim_timers::CrewStopwatchStart/Elapsed/Stop
// (our End too), which serve it as a call from the crew's range.
#pragma once

#include "core/game_access.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>

namespace opensc4::crew_sim {

inline constexpr std::size_t kObjectSize = 0x54;
inline constexpr std::size_t kSlots = 8;
inline constexpr std::size_t kJobSize = 0x80;
inline constexpr std::size_t kListNodeSize = 0xC;

// The messages the crew subscribes to (Init) and handles (DoMessage).
inline constexpr uint32_t kMsgOccupantRemoved = 0x99EF1143; // data: the occupant (GetVoid1)
inline constexpr uint32_t kMsgPreCityShutdown = 0x26D31EC2;
inline constexpr uint32_t kMsgFrame = 0x86AD10EE; // the frame service's: data 2 the frame's microseconds
// The messages PostMessage sends (with the sound service only).
inline constexpr uint32_t kMsgJobStarted = 0x89F10866, kMsgPieceDone = 0xA9F10E45;
// The work property: how many jobs hold a piece (IncrementWork, DecrementWork).
inline constexpr uint32_t kWorkProperty = 0xAA24AE57;
// Network flags FinishPiece sets (usable) and clears (0x4000, meaning not
// established; only when the service 0x00B43DC4's slot 22 answers 1).
inline constexpr uint32_t kFlagUsable = 0x10000000, kFlag4000 = 0x4000;
// The frame service's code for the crew (Start joins under it, Shutdown leaves).
inline constexpr uint32_t kFrameServiceCode = 0xC0;
// What the sounds plugin writes at addr::cSC4NetworkConstructionCrew_SoundJump
// (0x006071FC, the game has 0x74, a `je`): 0xEB, a `jmp`, so Init never
// stores the sound service.
inline constexpr uint8_t kSoundJumpGame = 0x74, kSoundJumpPlugin = 0xEB;

namespace field {
inline constexpr std::size_t kVtable = 0x0, kPieceMilliseconds = 0x4, kScale = 0x8, kCount = 0xC;
inline constexpr std::size_t kRefCount = 0x10, kInitialized = 0x14, kInFrameService = 0x15, kSound = 0x18;
inline constexpr std::size_t kUnused1C = 0x1C, kWorkerValueA = 0x20, kWorkerValueB = 0x24, kValueC = 0x28;
inline constexpr std::size_t kPieces = 0x2C, kSegments = 0x30, kRecords = 0x3C, kJobs = 0x48;
} // namespace field

namespace job {
inline constexpr std::size_t kMilliseconds = 0x0, kState = 0x4, kPiece = 0x8, kEffect = 0xC;
inline constexpr std::size_t kPedestrians = 0x10, kPieces = 0x1C, kStopwatch = 0x20, kStopwatchSize = 0x18;
inline constexpr std::size_t kFlag38 = 0x38, kWorkerA = 0x3C, kWorkerB = 0x5C;
} // namespace job

namespace worker {
inline constexpr std::size_t kFlag = 0x0, kValue = 0x4, kVehicle = 0x8, kPoint = 0xC, kArrived = 0x18;
inline constexpr std::size_t kFlag19 = 0x19, kPiece = 0x1C;
} // namespace worker

// Byte offsets of the crew's vtable slots.
namespace slot {
inline constexpr std::size_t kAddRef = 0x4, kRelease = 0x8, kDoMessage = 0xC, kInit = 0x10, kShutdown = 0x14;
inline constexpr std::size_t kStaticInit = 0x18, kStaticShutdown = 0x1C;
} // namespace slot

// Slots of other classes the crew calls (byte offsets; argument counts from
// the pushes before each call).
namespace piece_slot { // a network piece (cISC4NetworkOccupant, src/core/netocc_sim.h)
inline constexpr std::size_t kSetNetworkFlag = 0x28, kClearNetworkFlag = 0x2C; // one argument each
inline constexpr std::size_t kAsOccupant = 0xC0;                               // none: its cISC4Occupant
inline constexpr std::size_t kGetOccupiedCell = 0xEC;                          // two: x*, z*
} // namespace piece_slot
namespace occupant_slot { // src/core/occbase_sim.h
inline constexpr std::size_t kAsPropertyHolder = 0x18, kSetVisibility = 0x48; // none; two
} // namespace occupant_slot
inline constexpr std::size_t kHolderAddProperty = 0x28; // four: the property, 0, 1, 0
namespace message_slot { // the framework's standard message (vtable 0x00ACEC00)
inline constexpr std::size_t kGetType = 0x10, kSetType = 0x14, kGetVoid1 = 0x28, kSetData1 = 0x2C;
inline constexpr std::size_t kGetData2 = 0x38, kSetData2 = 0x40, kSetData3 = 0x54, kSetIGZUnknown = 0x84;
} // namespace message_slot
namespace server_slot { // the message server 0x00B43CCC
inline constexpr std::size_t kMessagePost = 0x10, kAddNotification = 0x14, kRemoveNotification = 0x18;
} // namespace server_slot
namespace manager_slot { // the network manager (SC4::GetNetworkManager)
inline constexpr std::size_t kRegisterConstructionCrew = 0x40, kUnregisterConstructionCrew = 0x44;
} // namespace manager_slot
namespace frame_slot { // the frame service 0x86AD10EC (SC4::GetService86AD10EC)
inline constexpr std::size_t kAdd = 0x14, kRemove = 0x18; // two: the target, the code
} // namespace frame_slot
// The unnamed service at 0x00B43DC4 (SC4::Service43DC4): FinishPiece clears
// a piece's 0x4000 flag when its slot 22 answers 1.
inline constexpr std::size_t kService43DC4Slot22 = 0x58;

// The network tool's tuning (0x00B452C8 + type * 0x114): the six values
// PlaceAllSegments hands to Init, as offsets into one type's record.
namespace tuning {
inline constexpr std::size_t kRecordSize = 0x114;
inline constexpr std::size_t kCount = 0x1C, kSeconds = 0xC8, kScale = 0xCC, kA = 0xD0, kB = 0xD4, kC = 0xD8;
} // namespace tuning

// The game's list node (std::list) and vector.
struct ListNode {
    ListNode* next;
    ListNode* prev;
    void* value;
};
struct Vector {
    char* begin;
    char* end;
    char* capacity;
};

inline uint32_t AddRefOf(void* object) { return Virtual<uint32_t>(object, 0x4); }
inline uint32_t ReleaseOf(void* object) { return Virtual<uint32_t>(object, 0x8); }

// A game function at its address: thiscall with `self` in ECX, or cdecl.
template <typename R, typename... Args>
inline R CallThis(std::uintptr_t address, void* self, Args... args)
{
    return reinterpret_cast<R(__thiscall*)(void*, Args...)>(address)(self, args...);
}

template <typename R, typename... Args>
inline R CallCdecl(std::uintptr_t address, Args... args)
{
    return reinterpret_cast<R(__cdecl*)(Args...)>(address)(args...);
}

// Logs "crew: first <name>" once per flag (crew_life.cpp).
void FirstCall(const char* name, bool& logged);

// A detour of a group: the game's address and our function. Skipped says
// whether OPENSC4_CREW_SKIP (function addresses, hex, eight digits) lists
// the address; InstallDetours installs a group's list but for those and logs
// "crew <what>: <n> detours" (crew_life.cpp).
struct Detour {
    std::uintptr_t address;
    void* ours;
};
#define OPENSC4_CREW_DETOUR(address, fn) {address, reinterpret_cast<void*>(&fn)}
bool Skipped(std::uintptr_t address);
bool InstallDetourList(const Detour* detours, std::size_t count, const char* what);
template <std::size_t N>
inline bool InstallDetours(const Detour (&detours)[N], const char* what)
{
    return InstallDetourList(detours, N, what);
}

// game.quiet.network with our Init (src/core/game_quiet.cpp, docs/quiet.md).
// Two detours cannot share Init's entry 0x00607190, so when
// simulators.crew.life has put our Init there, the wrapper does not hook:
// it hands our Init an observer instead, and our Init then withholds the
// sound service itself (the wrapper's work) and tells the observer after
// every call whether it did. Our group installs at PreAppInit, before the
// wrapper (PostAppInit), so InitIsOurs is settled when the wrapper asks.
//
// InitIsOurs: true once our Init sits at the game's entry.
// SetQuietObserver: non-null turns the switch on; `withheld` is true for a
// call that would have stored the sound service and did not because of the
// switch (not for one the plugin's byte already stopped, nor one with no
// service, nor one that returned early: the same calls the wrapper counts).
using QuietObserver = void (*)(bool withheld);
bool InitIsOurs();
void SetQuietObserver(QuietObserver observer);

} // namespace opensc4::crew_sim
