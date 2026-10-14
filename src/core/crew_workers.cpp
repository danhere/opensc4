// Our cSC4NetworkConstructionCrew, part C2a (build 641; the class map, the
// job record and the worker record are in src/core/crew_sim.h, the slots of
// the classes the workers call in src/core/crew_workers.h): the workers, the
// dust effect and the job's End. Group simulators.crew.workers: Direction,
// FindPathSide, MakeWorkerVehicle, DismissWorker, MakeDustEffect,
// ChooseWorkerPath, PlaceWorker, StartWorker, AdvanceWorker,
// AdvanceWorkerToPiece, TickWorker, StartJobWorkers (12 detours); group
// simulators.crew.jobend: EndJob (1). Each is a detour at the game's
// address, and every call from one of them to another function of the crew
// goes through the game's address, so a group that is off, or a function on
// the skip list OPENSC4_CREW_SKIP, is the game's.
//
// A job has two workers (job + 0x3C and + 0x5C, crew_sim.h); a worker drives
// a vehicle (an automaton of the vehicle manager) along the job's pieces.
// The worker's point (+0xC) is where it heads; +0x18 says it has arrived,
// +0x19 that its vehicle follows a path; +0x1C is the piece it is on.
//
// The clock: EndJob stops the job's stopwatch. determinism.sim_timers serves
// the crew's stopwatch calls by the caller's return address (0x00605490 to
// 0x00608C50); a call from core.dll is outside that range, so EndJob stops it
// through sim_timers::CrewStopwatchStop (the crew's per-call simulation
// counter whatever the caller, the game's SC4Stopwatch::Stop 0x008905F0 when
// the switch is off). No other function here reads a clock: TickWorker gets
// the frame's microseconds from Tick (0x00607C96, 0x00607CB9) and reads no
// clock of its own.
//
// Random: ChooseWorkerPath hands the static generator 0x00B43E88 to the path
// information's slot 9 (0x0053FB80), which draws from it; the same object is
// passed, so the draws are the game's, in the game's order. Nothing else
// here draws.
//
// Floats: the game's FPU runs at 53 bits, so every intermediate is a double
// in the asm's operand order; values the asm stores as floats are floats
// here. The constants are read from the game's memory at each use, as the
// asm reads them: 0.0625 (0x00AA7458), 1e-5 (0x00A99F20), 1.0 (0x00B0B2E4,
// in .data), 2^32 (0x00A80AA8), 0.6 (0x00A9809C).
//
// Registers: the detours are plain functions and clobber ECX and EDX. The
// game's functions that keep EDX (and ECX at a known value) on a path that
// calls nothing: DismissWorker with no vehicle and no piece (ECX 0),
// ChooseWorkerPath for a null piece (ECX the crew), AdvanceWorker and
// AdvanceWorkerToPiece for a worker without a vehicle (ECX the crew; the
// second with a vehicle but no piece: ECX 0), TickWorker for a worker
// without a vehicle or one that has arrived (ECX the crew). Every caller
// loads ECX and EDX before it reads them (0x006062B6, 0x00606394,
// 0x00606426, 0x006064D6, 0x00606702, 0x00606809, 0x00606814, 0x00607B9A,
// 0x00607E13, 0x00607E23; 0x00605FE0, 0x00606002, 0x00606314, 0x006064A6;
// 0x00606574, 0x00606722, 0x00607B5C, 0x00607B81; 0x00607C9B, 0x00607CBE).
//
// Calls, each count from the asm's pushes for that call and the callee's
// own `ret N`:
//   - this class at its address: Direction two (ret 8), FindPathSide three
//     (ret 0xC), MakeWorkerVehicle two (ret 8), DismissWorker one (ret 4),
//     ChooseWorkerPath six (ret 0x18), PlaceWorker four (ret 0x10),
//     StartWorker, AdvanceWorker and AdvanceWorkerToPiece four (ret 0x10),
//     NextPiece 0x00605BB0 four (ret 0x10; AL), FinishPiece 0x00605870 (cdecl,
//     one), FinishPieces 0x00605B30 (cdecl, two);
//   - the city's (SC4::City) 0xAC and 0xB0 none; the vehicle manager's 0x3C
//     seven (0x00505370, ret 0x1C) and the pedestrian manager's and vehicle
//     manager's 0x20 two (0x0054D690, ret 8);
//   - a vehicle's 0x1C, 0x3C, 0xA0, 0x22C one, 0x40, 0x78, 0x84 two, 0x210
//     four, 0x214 six (AL), 0x4 and 0x8 none;
//   - a piece's 0xC0, 0xC4, 0x5C, 0x4, 0x8 none, 0xEC two; its +4 part's 0x20
//     one; an occupant's 0x20 one, 0x28 two, 0x48 two;
//   - the path information's (vtable 0x00A99E70) 0xC two (the path or
//     null), 0x10 five (EAX tested whole), 0x24 six, 0x58 two, 0x7C four,
//     0x94 three (its EAX stored);
//   - the effects manager's (SC4::Service43D1C) 0x1C two; an effect's 0xC,
//     0x10, 0x20 one, 0x8 none; the terrain's (SC4::Terrain) 0x88 two (AL);
//   - SC4::stl_allocate 0x0090CF54 and SC4::Free 0x0090CF63 (cdecl, one),
//     cRZBaseString::Sprintf 0x0090F574 (cdecl, three pushed: `add esp, 0xC`).
#include "core/crew_workers.h"

#include "core/crew_sim.h"
#include "core/registry.h"
#include "core/sim_timers.h"

#include <opensc4/addresses.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace opensc4::crew_sim {
namespace {

// The game's string (SC4String: vtable, begin, end, capacity, references).
struct GameString {
    std::uintptr_t vtable;
    char* begin;
    char* end;
    char* capacity;
    uint32_t references;
};

void* City() { return Global<void*>(addr::SC4_City); }

void* VehicleManager() { return Virtual<void*>(City(), kCityVehicleManager); }

uint32_t PointDefault() { return Global<uint32_t>(addr::cSC4NetworkConstructionCrew_JobPointDefault); }

float GameFloat(std::uintptr_t address) { return Global<float>(address); }

void*& VehicleOf(char* worker) { return At<void*>(worker, worker::kVehicle); }

void*& PieceOf(char* worker) { return At<void*>(worker, worker::kPiece); }

// The game's float to int (0x009EEF04 here): truncation toward zero, the
// low 32 bits.
int32_t Truncate(double value) { return static_cast<int32_t>(static_cast<int64_t>(value)); }

// The functions of this class the workers call, at the game's addresses.
int32_t CallDirection(char* self, void* from, void* to)
{
    return CallThis<int32_t, void*, void*>(addr::cSC4NetworkConstructionCrew_Direction, self, from, to);
}

int32_t CallFindPathSide(char* self, void* piece, int32_t side, uint32_t flag)
{
    return CallThis<int32_t, void*, int32_t, uint32_t>(addr::cSC4NetworkConstructionCrew_FindPathSide, self, piece,
                                                       side, flag);
}

uint8_t CallChoosePath(char* self, void* piece, void* from, void* to, float* point, uint32_t* path, uint32_t flag)
{
    return CallThis<uint8_t, void*, void*, void*, float*, uint32_t*, uint32_t>(
        addr::cSC4NetworkConstructionCrew_ChooseWorkerPath, self, piece, from, to, point, path, flag);
}

void CallPlace(char* self, void* piece, void* next, char* worker, uint32_t flag)
{
    CallThis<void, void*, void*, char*, uint32_t>(addr::cSC4NetworkConstructionCrew_PlaceWorker, self, piece, next,
                                                  worker, flag);
}

void CallDismiss(char* self, char* worker)
{
    CallThis<void, char*>(addr::cSC4NetworkConstructionCrew_DismissWorker, self, worker);
}

uint8_t CallNextPiece(char* self, ListNode** list, void** current, void** out, uint8_t* adjacent)
{
    return CallThis<uint8_t, ListNode**, void**, void**, uint8_t*>(addr::cSC4NetworkConstructionCrew_NextPiece, self,
                                                                    list, current, out, adjacent);
}

void CallFinishPiece(void* piece) { CallCdecl<void, void*>(addr::cSC4NetworkConstructionCrew_FinishPiece, piece); }

// A piece's occupant shown (slot 0xC0, then the occupant's slot 0x48 with
// 1, 1).
void ShowPiece(void* piece)
{
    Virtual<void>(Virtual<void*>(piece, piece_slot::kAsOccupant), occupant_slot::kSetVisibility, 1u, 1u);
}

// ====================================================================================
// simulators.crew.workers
// ====================================================================================

// 0x00605490, ret 8: Direction(from, to), `this` not read. The two pieces'
// cells (slot 0xEC each: x, z). Not one apart (|dx| + |dz| != 1, in 32-bit
// arithmetic) -1; else dz < 0 1, dz > 0 3, dx < 0 0, dx > 0 2. Only caller
// ChooseWorkerPath (0x00605D91, 0x00605DA6).
int32_t __fastcall Direction(char*, void*, void* from, void* to)
{
    static bool logged = false;
    FirstCall("Direction", logged);
    int32_t fromX = 0, fromZ = 0, toX = 0, toZ = 0;
    Virtual<void>(from, piece_slot::kGetOccupiedCell, &fromX, &fromZ);
    Virtual<void>(to, piece_slot::kGetOccupiedCell, &toX, &toZ);
    const uint32_t dz = static_cast<uint32_t>(toZ) - static_cast<uint32_t>(fromZ);
    const uint32_t dx = static_cast<uint32_t>(toX) - static_cast<uint32_t>(fromX);
    const auto absolute = [](uint32_t value) {
        const uint32_t sign = static_cast<uint32_t>(static_cast<int32_t>(value) >> 31);
        return (value ^ sign) - sign;
    };
    if (absolute(dz) + absolute(dx) != 1)
        return -1;
    if (static_cast<int32_t>(dz) < 0)
        return 1;
    if (static_cast<int32_t>(dz) > 0)
        return 3;
    return static_cast<int32_t>((static_cast<uint32_t>(static_cast<int32_t>(dx) < 0) - 1u) & 2u);
}

// 0x00605530, ret 0xC: FindPathSide(piece, side, flag). The piece's path
// information (slot 0xC4; none: -1). For the sides side + 2, + 3, + 1 and
// + 0 (each modulo 4, signed as the game's `and 0x80000003` remainder), for
// the kinds 1 and the crew's +0x28, for 0 and 1: the information's slot 0x10
// (kind, side tried, side, k, 0), the two sides the other way round when the
// flag is clear; the first that answers non-zero (EAX whole) gives the side
// tried. None: -1. Callers: ChooseWorkerPath 0x00605DCA (flag 1) and
// 0x00605DE3 (flag 0).
int32_t __fastcall FindPathSide(char* self, void*, void* piece, int32_t side, uint32_t flag)
{
    static bool logged = false;
    FirstCall("FindPathSide", logged);
    const uint32_t kinds[2] = {1u, At<uint32_t>(self, field::kValueC)};
    const int32_t turns[4] = {2, 3, 5, 0};
    void* const path = Virtual<void*>(piece, kPieceGetPathInfo);
    if (!path)
        return -1;
    for (const int32_t turn : turns) {
        const int32_t tried = static_cast<int32_t>(static_cast<uint32_t>(turn) + static_cast<uint32_t>(side)) % 4;
        for (const uint32_t kind : kinds)
            for (uint32_t k = 0; k <= 1; ++k) {
                const uint32_t answer =
                    static_cast<uint8_t>(flag)
                        ? Virtual<uint32_t>(path, path_slot::kHasPath, kind, tried, side, k, 0u)
                        : Virtual<uint32_t>(path, path_slot::kHasPath, kind, side, tried, k, 0u);
                if (answer != 0)
                    return tried;
            }
    }
    return -1;
}

// 0x00605740, ret 8: MakeWorkerVehicle(piece, worker), `this` not read. The
// piece's occupant's position (zeros without a piece); the vehicle manager
// (city slot 0xAC; none: nothing) makes an automaton (slot 0x3C: 0x4002, the
// position, 0x3B000, the key {0x5AD0E817, 0xBADB57F1, the worker's value},
// 0, 0x32, 0; none: nothing), its slot 0x78 (2, 0); it goes to the worker,
// referenced, +0x19 set, the piece to +0x1C (referenced; the piece there
// before is not released, as in the game). EAX is not read by the only
// caller PlaceWorker (0x00605F7E reads the worker's vehicle).
void __fastcall MakeWorkerVehicle(char*, void*, void* piece, char* worker)
{
    static bool logged = false;
    FirstCall("MakeWorkerVehicle", logged);
    float position[3] = {0.0f, 0.0f, 0.0f};
    if (piece)
        Virtual<void>(Virtual<void*>(piece, piece_slot::kAsOccupant), kOccupantGetPosition, position);
    const uint32_t key[3] = {kVehicleKeyType, kVehicleKeyGroup, At<uint32_t>(worker, worker::kValue)};
    void* const manager = VehicleManager();
    if (!manager)
        return;
    void* const vehicle =
        Virtual<void*>(manager, automata_slot::kMake, 0x4002u, position, 0x3B000u, key, 0u, 0x32u, 0u);
    if (!vehicle)
        return;
    Virtual<void>(vehicle, vehicle_slot::kSetMode, 2u, 0u);
    VehicleOf(worker) = vehicle;
    AddRefOf(vehicle);
    At<uint8_t>(worker, worker::kFlag19) = 1;
    PieceOf(worker) = piece;
    if (piece)
        AddRefOf(piece);
}

// 0x00605800, ret 4: DismissWorker(worker). With a vehicle: the vehicle
// manager asked; when both are there, the vehicle's slot 0xA0 (the crew) and
// the manager's slot 0x20 (the vehicle, 0); then the vehicle (read again)
// released. The piece released when there is one; both fields cleared. No
// caller reads EAX (C1's OnOccupantRemoved, AdvanceWorker,
// AdvanceWorkerToPiece, StartJobWorkers, EndJob, the job's NextPiece
// 0x00607B9A).
void __fastcall DismissWorker(char* self, void*, char* worker)
{
    static bool logged = false;
    FirstCall("DismissWorker", logged);
    if (VehicleOf(worker)) {
        void* const manager = VehicleManager();
        if (VehicleOf(worker) && manager) {
            Virtual<void>(VehicleOf(worker), vehicle_slot::kLeave, static_cast<void*>(self));
            Virtual<void>(manager, automata_slot::kRemove, VehicleOf(worker), 0u);
        }
        ReleaseOf(VehicleOf(worker));
    }
    if (void* const piece = PieceOf(worker))
        ReleaseOf(piece);
    PieceOf(worker) = nullptr;
    VehicleOf(worker) = nullptr;
}

// 0x00605940, ret 4: MakeDustEffect(piece), `this` not read. The effects
// manager's slot 7 with "road_construction_dust" (0x00AA7464); no effect:
// null. Else the effect placed by the block EffectTransform (crew_workers.h):
// identity, the position of the piece's occupant (slot 0x20) with its y
// replaced by the y of the second box corner (the occupant's slot 0x28, the
// second argument's float3), scale 1; then started (slot 3 with 0). The
// answer is the effect as the manager gave it (the job keeps it at +0xC:
// 0x006078C2, 0x00607B3F).
void* __fastcall MakeDustEffect(char*, void*, void* piece)
{
    static bool logged = false;
    FirstCall("MakeDustEffect", logged);
    void* effect = nullptr;
    Virtual<uint8_t>(Global<void*>(addr::SC4_Service43D1C), kEffectsMake,
                     reinterpret_cast<const char*>(addr::cSC4NetworkConstructionCrew_DustEffectName), &effect);
    if (!effect)
        return nullptr;
    float position[3];
    Virtual<void>(Virtual<void*>(piece, piece_slot::kAsOccupant), kOccupantGetPosition, position);
    float boxA[3];
    float boxB[3];
    Virtual<void>(Virtual<void*>(piece, piece_slot::kAsOccupant), kOccupantGetBounds, boxA, boxB);
    EffectTransform transform = {};
    transform.flag0 = 1;
    transform.flag1 = 1;
    transform.matrix[0] = 1.0f;
    transform.matrix[4] = 1.0f;
    transform.matrix[8] = 1.0f;
    transform.position[0] = position[0];
    transform.position[1] = boxB[1];
    transform.position[2] = position[2];
    transform.scale = 1.0f;
    Virtual<void>(effect, kEffectSetTransform, &transform);
    Virtual<void>(effect, kEffectStart, 0u);
    return effect;
}

// 0x00605D30, ret 0x18: ChooseWorkerPath(piece, from, to, point*, path*,
// swap). False (AL 0) for a null piece, nothing written. The piece's path
// information (slot 0xC4); with one and a crew whose +0x28 is 5, its slot
// 0x7C (0xFF, 1, 5, 1). The sides: toward `from` and toward `to`
// (Direction, 0xFF when that piece is null; Direction's -1 for pieces not one
// apart is not 0xFF and passes as a side, as in the game); one of them 0xFF
// and the other not: FindPathSide gives it (flag 1 for the first, 0 for the
// second); either still 0xFF (or both were): the pair is not used. With
// `swap` (its low byte) the two sides change places. Without path
// information: the message "Network piece with ID %08x has no path info!"
// formatted into a game string and dropped (no log; its memory is the
// game's allocator's, as in the game), the piece's position into *point
// (the slot 0x20 of its +4 part), *path 0, false. With it: for the kinds 1
// and the crew's +0x28, while no path is found: with the pair, *path the
// information's slot 0x94 (kind, side, side); without, its slot 0x24 (kind,
// side, side, the generator 0x00B43E88, path, 0), which writes *path and
// draws; the path is its slot 0xC (*path, 0). Found: its end point into
// *point (slot 0x58) and true (AL 1). Not found: the piece's position, *path
// 0, false. Callers: PlaceWorker (0x00605FDB, 0x00605FFD), AdvanceWorker
// (0x0060630F), AdvanceWorkerToPiece (0x006064A1).
uint8_t __fastcall ChooseWorkerPath(char* self, void*, void* piece, void* from, void* to, float* point,
                                    uint32_t* path, uint32_t swap)
{
    static bool logged = false;
    FirstCall("ChooseWorkerPath", logged);
    if (!piece)
        return 0;
    void* const info = Virtual<void*>(piece, kPieceGetPathInfo);
    if (info && At<uint32_t>(self, field::kValueC) == 5)
        Virtual<void>(info, path_slot::kMark, 0xFFu, 1u, 5u, 1u);
    int32_t sideFrom = 0xFF;
    int32_t sideTo = 0xFF;
    if (from)
        sideFrom = CallDirection(self, piece, from);
    if (to)
        sideTo = CallDirection(self, piece, to);
    uint8_t pair = 1;
    if (sideFrom == 0xFF) {
        if (sideTo == sideFrom) {
            pair = 0;
        } else {
            sideFrom = CallFindPathSide(self, piece, sideTo, 1u);
            if (sideFrom == 0xFF || sideTo == 0xFF)
                pair = 0;
        }
    } else {
        if (sideTo == 0xFF)
            sideTo = CallFindPathSide(self, piece, sideFrom, 0u);
        if (sideFrom == 0xFF || sideTo == 0xFF)
            pair = 0;
    }
    if (static_cast<uint8_t>(swap)) {
        const int32_t first = sideFrom;
        sideFrom = sideTo;
        sideTo = first;
    }
    if (!info) {
        GameString text = {};
        char* const body = CallCdecl<char*, uint32_t>(addr::SC4_stl_allocate, 8u);
        text.capacity = body + 8;
        text.begin = body;
        text.end = body;
        *body = 0;
        text.vtable = addr::SC4String_Vtable;
        text.references = 0;
        const uint32_t id = Virtual<uint32_t>(piece, kPieceGetId);
        reinterpret_cast<void(__cdecl*)(GameString*, const char*, ...)>(addr::cRZBaseString_Sprintf)(
            &text, reinterpret_cast<const char*>(addr::cSC4NetworkConstructionCrew_NoPathFormat), id);
        Virtual<void>(static_cast<char*>(piece) + 4, kOccupantGetPosition, point);
        *path = 0;
        text.vtable = addr::SC4String_Vtable;
        if (text.begin)
            CallCdecl<void, void*>(addr::SC4_Free, text.begin);
        return 0;
    }
    const uint32_t kinds[2] = {1u, At<uint32_t>(self, field::kValueC)};
    void* found = nullptr;
    for (const uint32_t kind : kinds) {
        if (found)
            break;
        if (pair)
            *path = Virtual<uint32_t>(info, path_slot::kFirst, kind, sideFrom, sideTo);
        else
            Virtual<void>(info, path_slot::kChoose, kind, sideFrom, sideTo,
                          reinterpret_cast<void*>(addr::cSC4NetworkConstructionCrew_Random), path, 0u);
        found = Virtual<void*>(info, path_slot::kFindPath, *path, 0u);
    }
    if (found) {
        Virtual<void>(info, path_slot::kEndPoint, found, point);
        return 1;
    }
    Virtual<void>(static_cast<char*>(piece) + 4, kOccupantGetPosition, point);
    *path = 0;
    return 0;
}

// 0x00605F60, ret 0x10: PlaceWorker(piece, next, worker, swap). No vehicle:
// MakeWorkerVehicle (piece, worker); still none: nothing. No vehicle
// manager: nothing. Two points, every word the one at 0x00B43E64: `start`
// from ChooseWorkerPath(piece, null, next, start, path, swap); with a next
// piece `end` from ChooseWorkerPath(next, piece, null, end, the slot of the
// fourth argument, swap) and the answer from that call, else `end` = `start`
// and the first answer. The worker's piece released and replaced by `piece`
// (referenced). The cell of `start` (x and z times 0.0625, 0x009EEF04
// truncation). Without a path: the speed (vehicle slot 0x84) 1.0f when the
// terrain (when there is one) answers false to its slot 0x88 for that cell,
// else 0. The worker's point = `end`; the direction end - start, normalised
// when longer than 1e-5 (1.0 / length), else given as it is to the vehicle's
// slot 0x3C; the vehicle placed at `start` with it (slot 0x40). With a path:
// the vehicle follows it (slot 0x210: piece, the first call's path, 0, 1;
// slot 0x22C 0), arrived when there is no next piece, +0x19 set. Without: the
// worker's piece finished (FinishPiece), arrived, +0x19 cleared. No caller
// reads EAX (0x0060621A, 0x006062C6, 0x00606373, 0x00606436, 0x00607B89).
void __fastcall PlaceWorker(char* self, void*, void* piece, void* next, char* worker, uint32_t swap)
{
    static bool logged = false;
    FirstCall("PlaceWorker", logged);
    if (!VehicleOf(worker)) {
        CallThis<void, void*, char*>(addr::cSC4NetworkConstructionCrew_MakeWorkerVehicle, self, piece, worker);
        if (!VehicleOf(worker))
            return;
    }
    if (!VehicleManager())
        return;
    const uint32_t point = PointDefault();
    uint32_t end[3] = {point, point, point};
    uint32_t start[3] = {point, point, point};
    uint32_t firstPath = 0;
    uint32_t secondPath = 0; // the game's out slot is the fourth argument's (not read after)
    uint8_t found = CallChoosePath(self, piece, nullptr, next, reinterpret_cast<float*>(start), &firstPath, swap);
    if (next) {
        found = CallChoosePath(self, next, piece, nullptr, reinterpret_cast<float*>(end), &secondPath, swap);
    } else {
        end[0] = start[0];
        end[1] = start[1];
        end[2] = start[2];
    }
    if (void* const old = PieceOf(worker))
        ReleaseOf(old);
    PieceOf(worker) = piece;
    if (piece)
        AddRefOf(piece);
    float startF[3];
    float endF[3];
    std::memcpy(startF, start, sizeof startF);
    std::memcpy(endF, end, sizeof endF);
    void* const terrain = Global<void*>(addr::SC4_Terrain);
    const double sixteenth = GameFloat(addr::SC4_FloatSixteenthAA7458);
    const int32_t cellX = Truncate(static_cast<double>(startF[0]) * sixteenth);
    const int32_t cellZ = Truncate(static_cast<double>(startF[2]) * sixteenth);
    if (!found) {
        if (terrain && !Virtual<uint8_t>(terrain, kTerrainSlot88, cellX, cellZ))
            Virtual<void>(VehicleOf(worker), vehicle_slot::kSetSpeed, 1.0f, 0u);
        else
            Virtual<void>(VehicleOf(worker), vehicle_slot::kSetSpeed, 0.0f, 0u);
    }
    std::memcpy(&At<uint32_t>(worker, worker::kPoint), end, sizeof end);
    float direction[3];
    direction[0] = static_cast<float>(static_cast<double>(endF[0]) - static_cast<double>(startF[0]));
    direction[1] = static_cast<float>(static_cast<double>(endF[1]) - static_cast<double>(startF[1]));
    const double dz = static_cast<double>(endF[2]) - static_cast<double>(startF[2]);
    direction[2] = static_cast<float>(dz);
    const double length = std::sqrt((dz * static_cast<double>(direction[2]) +
                                     static_cast<double>(direction[1]) * static_cast<double>(direction[1])) +
                                    static_cast<double>(direction[0]) * static_cast<double>(direction[0]));
    if (length > static_cast<double>(GameFloat(addr::SC4_FloatHundredThousandth))) {
        const double inverse = static_cast<double>(GameFloat(addr::SC4_FloatOneB0B2E4)) / length;
        direction[0] = static_cast<float>(static_cast<double>(direction[0]) * inverse);
        direction[1] = static_cast<float>(static_cast<double>(direction[1]) * inverse);
        direction[2] = static_cast<float>(static_cast<double>(direction[2]) * inverse);
    } else {
        Virtual<void>(VehicleOf(worker), vehicle_slot::kSetDirection, direction);
    }
    Virtual<void>(VehicleOf(worker), vehicle_slot::kPlace, startF, direction);
    if (found) {
        Virtual<void>(VehicleOf(worker), vehicle_slot::kFollowPath, piece, firstPath, 0u, 1u);
        Virtual<void>(VehicleOf(worker), vehicle_slot::kStartPath, 0u);
        At<uint8_t>(worker, worker::kArrived) = next == nullptr;
        At<uint8_t>(worker, worker::kFlag19) = 1;
        return;
    }
    if (void* const own = PieceOf(worker))
        CallFinishPiece(own);
    At<uint8_t>(worker, worker::kArrived) = 1;
    At<uint8_t>(worker, worker::kFlag19) = 0;
}

// 0x006061B0, ret 0x10: StartWorker(list, piece, worker, swap). NextPiece
// (list, piece, next, adjacent) with next null and adjacent 0 first; found
// and not adjacent: the next piece released and not used. PlaceWorker
// (piece, next, worker, swap); arrived when there is no next piece; the
// worker's piece (when there is one) shown (its occupant's slot 0x48, 1, 1);
// the next piece released. Callers: StartJobWorkers 0x0060673B, the job's
// Begin 0x0060790C; neither reads EAX (0x00606740 returns it from
// StartJobWorkers, whose callers do not read it; Begin's callers in Start,
// 0x006084BE, 0x006084CA, 0x006089DC, do not either).
void __fastcall StartWorker(char* self, void*, ListNode** list, void* piece, char* worker, uint32_t swap)
{
    static bool logged = false;
    FirstCall("StartWorker", logged);
    void* next = nullptr;
    uint8_t adjacent = 0;
    void* current = piece;
    void* kept = nullptr;
    if (CallNextPiece(self, list, &current, &next, &adjacent) && !adjacent) {
        if (next)
            ReleaseOf(next);
    } else {
        kept = next;
    }
    CallPlace(self, piece, kept, worker, swap);
    At<uint8_t>(worker, worker::kArrived) = kept == nullptr;
    if (void* const own = PieceOf(worker))
        ShowPiece(own);
    if (kept)
        ReleaseOf(kept);
}

// 0x00606250, ret 0x10: AdvanceWorker(list, worker, jobPiece, swap), AL the
// answer. No vehicle or no piece: false. NextPiece after the worker's piece
// (its out and adjacent in the game's argument slots); not in the list: the
// worker dismissed, false. The next piece: for worker B (its byte +0 set)
// when it is the job's current piece: dismissed, false, placed on it
// (PlaceWorker with no next) and the next piece released; not one apart:
// the same without the dismissal, true. One apart: ChooseWorkerPath (next,
// the worker's piece, null; the point it writes is not used); a path: the
// vehicle follows it (slot 0x210: next, the path, 0, 1; slot 0x22C 0), the
// worker's piece becomes the next one (old released, new referenced) and
// the reference NextPiece gave released; no path: placed on it, arrived, the
// next piece released; true. NextPiece answers true only with a next piece
// (C1's translation: AL is whether *out is set), so `next` is never null
// where it is used. Only caller TickWorker 0x0060656F (EAX not read).
uint8_t __fastcall AdvanceWorker(char* self, void*, ListNode** list, char* worker, void* jobPiece, uint32_t swap)
{
    static bool logged = false;
    FirstCall("AdvanceWorker", logged);
    uint8_t result = 1;
    if (!VehicleOf(worker) || !PieceOf(worker))
        return 0;
    void* next = nullptr; // in the game the first argument's slot, written by NextPiece when it answers true
    uint8_t adjacent = 0; // the second argument's slot, the same
    if (!CallNextPiece(self, list, &PieceOf(worker), &next, &adjacent)) {
        CallDismiss(self, worker);
        return 0;
    }
    if (At<uint8_t>(worker, worker::kFlag) != 0 && next == jobPiece) {
        CallDismiss(self, worker);
        result = 0;
    } else if (adjacent) {
        const uint32_t point = PointDefault();
        uint32_t at[3] = {point, point, point};
        uint32_t path = 0;
        if (CallChoosePath(self, next, PieceOf(worker), nullptr, reinterpret_cast<float*>(at), &path, swap)) {
            Virtual<void>(VehicleOf(worker), vehicle_slot::kFollowPath, next, path, 0u, 1u);
            Virtual<void>(VehicleOf(worker), vehicle_slot::kStartPath, 0u);
            if (void* const old = PieceOf(worker))
                ReleaseOf(old);
            PieceOf(worker) = next;
            AddRefOf(next);
            ReleaseOf(next);
            return result;
        }
        CallPlace(self, next, nullptr, worker, swap);
        At<uint8_t>(worker, worker::kArrived) = 1;
        ReleaseOf(next);
        return result;
    }
    CallPlace(self, next, nullptr, worker, swap);
    ReleaseOf(next);
    return result;
}

// 0x006063C0, ret 0x10: AdvanceWorkerToPiece(list, jobPiece, worker, swap),
// AL the answer. No vehicle or no piece: false. NextPiece after the worker's
// piece; not in the list: dismissed, false. Worker B on the job's current
// piece: dismissed, false, then as `not one apart`. Not one apart: placed on
// the next piece (PlaceWorker with no next), arrived. One apart:
// ChooseWorkerPath (next, the worker's piece, null; the path into the
// fourth argument's slot); a path: the worker's point is the point it gave,
// not arrived (the worker's piece and the vehicle left as they are); no
// path: placed on it, arrived. Then for worker A (byte +0 clear) the next
// piece shown (its occupant's slot 0x48, 1, 1); the next piece released;
// true unless dismissed. Callers: StartJobWorkers 0x0060671D, the job's
// NextPiece 0x00607B7C (neither reads EAX: 0x00606722 returns it,
// 0x00607B81 jumps to a load).
uint8_t __fastcall AdvanceWorkerToPiece(char* self, void*, ListNode** list, void* jobPiece, char* worker,
                                        uint32_t swap)
{
    static bool logged = false;
    FirstCall("AdvanceWorkerToPiece", logged);
    uint8_t result = 1;
    if (!VehicleOf(worker) || !PieceOf(worker))
        return 0;
    void* next = nullptr; // in the game the first argument's slot, written by NextPiece when it answers true
    uint8_t adjacent = 0; // the third argument's slot, the same
    if (!CallNextPiece(self, list, &PieceOf(worker), &next, &adjacent)) {
        CallDismiss(self, worker);
        return 0;
    }
    bool place = true;
    if (At<uint8_t>(worker, worker::kFlag) != 0 && next == jobPiece) {
        CallDismiss(self, worker);
        result = 0;
    } else if (adjacent) {
        const uint32_t point = PointDefault();
        uint32_t at[3] = {point, point, point};
        uint32_t path = 0; // the game's out slot is the fourth argument's (it keeps `swap` in a register)
        if (CallChoosePath(self, next, PieceOf(worker), nullptr, reinterpret_cast<float*>(at), &path, swap)) {
            std::memcpy(&At<uint32_t>(worker, worker::kPoint), at, sizeof at);
            At<uint8_t>(worker, worker::kArrived) = 0;
            place = false;
        }
    }
    if (place) {
        CallPlace(self, next, nullptr, worker, swap);
        At<uint8_t>(worker, worker::kArrived) = 1;
    }
    if (At<uint8_t>(worker, worker::kFlag) == 0)
        ShowPiece(next);
    ReleaseOf(next);
    return result;
}

// 0x00606500, ret 0xC: TickWorker(microseconds, worker, job). The step is
// the microseconds (unsigned: 2^32 added to a negative int, the float at
// 0x00A80AA8) times the crew's +0x8 (a float, stored as one). Nothing for a
// worker without a vehicle or one that has arrived; the vehicle manager is
// asked (its answer not used) and the vehicle read again. Without a path
// (+0x19 clear): AdvanceWorker (the job's list, the worker, its current
// piece, its byte +0x38). Then (vehicle read again; none: nothing) its
// position P (slot 0x1C; the three words at 0x00B43E64 first); +0x19 is the
// vehicle's slot 0x214 (the step, P, P, 0, 0, 0); its position Q again.
// When the distance from Q to the worker's point is below the step (ordered,
// strictly), P is the point. The direction point - P, normalised (1.0 /
// its length as a float) when its length is above 1e-5 (else zero); the
// vehicle placed at P with it (slot 0x40); arrived when that length (as a
// float) is below 0.6. Callers: Tick 0x00607C96 and 0x00607CB9 (EAX not
// read).
void __fastcall TickWorker(char* self, void*, uint32_t microseconds, char* worker, char* job)
{
    static bool logged = false;
    FirstCall("TickWorker", logged);
    double time = static_cast<double>(static_cast<int32_t>(microseconds));
    if (static_cast<int32_t>(microseconds) < 0)
        time += static_cast<double>(GameFloat(addr::SC4_FloatTwoTo32));
    const float step = static_cast<float>(time * static_cast<double>(At<float>(self, field::kScale)));
    if (!VehicleOf(worker) || At<uint8_t>(worker, worker::kArrived))
        return;
    VehicleManager();
    if (!VehicleOf(worker))
        return;
    if (!At<uint8_t>(worker, worker::kFlag19))
        CallThis<uint8_t, ListNode**, char*, void*, uint32_t>(
            addr::cSC4NetworkConstructionCrew_AdvanceWorker, self, &At<ListNode*>(job, job::kPieces), worker,
            At<void*>(job, job::kPiece), static_cast<uint32_t>(At<uint8_t>(job, job::kFlag38)));
    if (!VehicleOf(worker))
        return;
    const uint32_t point = PointDefault();
    uint32_t atBits[3] = {point, point, point};
    Virtual<void>(VehicleOf(worker), vehicle_slot::kGetPosition, atBits);
    At<uint8_t>(worker, worker::kFlag19) =
        Virtual<uint8_t>(VehicleOf(worker), vehicle_slot::kMove, step, atBits, atBits, 0u, 0u, 0u);
    const uint32_t point2 = PointDefault();
    uint32_t nowBits[3] = {point2, point2, point2};
    Virtual<void>(VehicleOf(worker), vehicle_slot::kGetPosition, nowBits);
    float now[3];
    std::memcpy(now, nowBits, sizeof now);
    const float* const target = &At<float>(worker, worker::kPoint);
    const double d0 = static_cast<double>(target[0]) - static_cast<double>(now[0]);
    const double d1 = static_cast<double>(target[1]) - static_cast<double>(now[1]);
    const double d2 = static_cast<double>(target[2]) - static_cast<double>(now[2]);
    const double remaining = std::sqrt((d2 * d2 + d1 * d1) + d0 * d0);
    if (remaining < static_cast<double>(step))
        std::memcpy(atBits, &At<uint32_t>(worker, worker::kPoint), sizeof atBits);
    float at[3];
    std::memcpy(at, atBits, sizeof at);
    const double e0 = static_cast<double>(target[0]) - static_cast<double>(at[0]);
    const float e1 = static_cast<float>(static_cast<double>(target[1]) - static_cast<double>(at[1]));
    const double e2 = static_cast<double>(target[2]) - static_cast<double>(at[2]);
    const double length =
        std::sqrt((e2 * e2 + static_cast<double>(e1) * static_cast<double>(e1)) + e0 * e0);
    const float lengthF = static_cast<float>(length);
    float direction[3];
    if (length > static_cast<double>(GameFloat(addr::SC4_FloatHundredThousandth))) {
        const double inverse = static_cast<double>(GameFloat(addr::SC4_FloatOneB0B2E4)) / static_cast<double>(lengthF);
        direction[0] = static_cast<float>(inverse * e0);
        direction[1] = static_cast<float>(static_cast<double>(e1) * inverse);
        direction[2] = static_cast<float>(e2 * inverse);
    } else {
        direction[0] = 0.0f;
        direction[1] = 0.0f;
        direction[2] = 0.0f;
    }
    Virtual<void>(VehicleOf(worker), vehicle_slot::kPlace, at, direction);
    if (static_cast<double>(lengthF) < static_cast<double>(GameFloat(addr::SC4_Float0_6)))
        At<uint8_t>(worker, worker::kArrived) = 1;
}

// 0x006066E0, ret 4: StartJobWorkers(job), worker A only. With a vehicle:
// dismissed first when both workers are on the same piece; still with one:
// AdvanceWorkerToPiece (the job's list, its current piece, worker A, its
// byte +0x38). Else StartWorker with the same. EAX is the callee's; no
// caller reads it (0x00607B5C loads AL from the stack, 0x00607D51 loads).
void __fastcall StartJobWorkers(char* self, void*, char* job)
{
    static bool logged = false;
    FirstCall("StartJobWorkers", logged);
    char* const workerA = job + job::kWorkerA;
    if (VehicleOf(workerA)) {
        if (PieceOf(workerA) == At<void*>(job, job::kWorkerB + worker::kPiece))
            CallDismiss(self, workerA);
        if (VehicleOf(workerA)) {
            CallThis<uint8_t, ListNode**, void*, char*, uint32_t>(
                addr::cSC4NetworkConstructionCrew_AdvanceWorkerToPiece, self, &At<ListNode*>(job, job::kPieces),
                At<void*>(job, job::kPiece), workerA, static_cast<uint32_t>(At<uint8_t>(job, job::kFlag38)));
            return;
        }
    }
    CallThis<void, ListNode**, void*, char*, uint32_t>(addr::cSC4NetworkConstructionCrew_StartWorker, self,
                                                       &At<ListNode*>(job, job::kPieces), At<void*>(job, job::kPiece),
                                                       workerA, static_cast<uint32_t>(At<uint8_t>(job, job::kFlag38)));
}

// ====================================================================================
// simulators.crew.jobend
// ====================================================================================

// 0x00606750, ret 8: EndJob(job, final). The dust effect stopped (slot 4
// with 0), released and cleared; the current piece finished (FinishPiece),
// released and cleared; every pedestrian (the count taken once, the vector
// read again for each) removed by the pedestrian manager (city slot 0xB0,
// its slot 0x20 with 0) and released, the vector emptied (its end = its
// begin: the game's erase of the whole range, whose move is dead code). With
// the final byte: both workers dismissed, the job's pieces finished and
// released (FinishPieces with 1), the list's nodes freed (the head read
// again after each) and the head linked to itself. Then +0 (the milliseconds
// per piece) 0 and the stopwatch stopped on the crew's simulation clock
// (sim_timers::CrewStopwatchStop). Callers: Shutdown 0x0060799A (1), the
// job's NextPiece 0x00607AFA and 0x00607BFA (0); none reads EAX, ECX or EDX
// after the call.
void __fastcall EndJob(char* self, void*, char* job, uint32_t final)
{
    static bool logged = false;
    FirstCall("EndJob", logged);
    if (At<void*>(job, job::kEffect)) {
        Virtual<void>(At<void*>(job, job::kEffect), kEffectStop, 0u);
        ReleaseOf(At<void*>(job, job::kEffect));
        At<void*>(job, job::kEffect) = nullptr;
    }
    if (void* const piece = At<void*>(job, job::kPiece)) {
        CallFinishPiece(piece);
        ReleaseOf(At<void*>(job, job::kPiece));
        At<void*>(job, job::kPiece) = nullptr;
    }
    void* const pedestrians = Virtual<void*>(City(), kCityPedestrianManager);
    Vector* const walkers = reinterpret_cast<Vector*>(job + job::kPedestrians);
    const int32_t count = static_cast<int32_t>(walkers->end - walkers->begin) >> 2;
    for (int32_t i = 0; i < count; ++i) {
        Virtual<void>(pedestrians, automata_slot::kRemove, reinterpret_cast<void**>(walkers->begin)[i], 0u);
        ReleaseOf(reinterpret_cast<void**>(walkers->begin)[i]);
    }
    walkers->end = walkers->begin;
    if (static_cast<uint8_t>(final)) {
        CallDismiss(self, job + job::kWorkerA);
        CallDismiss(self, job + job::kWorkerB);
        ListNode** const headAt = &At<ListNode*>(job, job::kPieces);
        CallCdecl<void, ListNode**, uint32_t>(addr::cSC4NetworkConstructionCrew_FinishPieces, headAt, 1u);
        ListNode* node = (*headAt)->next;
        while (node != *headAt) {
            ListNode* const after = node->next;
            CallCdecl<void, void*>(addr::SC4_Free, node);
            node = after;
        }
        (*headAt)->next = *headAt;
        (*headAt)->prev = *headAt;
    }
    At<uint32_t>(job, job::kMilliseconds) = 0;
    sim_timers::CrewStopwatchStop(job + job::kStopwatch);
}

// ====================================================================================
// Registration
// ====================================================================================

const Detour kWorkers[] = {
    OPENSC4_CREW_DETOUR(addr::cSC4NetworkConstructionCrew_Direction, Direction),
    OPENSC4_CREW_DETOUR(addr::cSC4NetworkConstructionCrew_FindPathSide, FindPathSide),
    OPENSC4_CREW_DETOUR(addr::cSC4NetworkConstructionCrew_MakeWorkerVehicle, MakeWorkerVehicle),
    OPENSC4_CREW_DETOUR(addr::cSC4NetworkConstructionCrew_DismissWorker, DismissWorker),
    OPENSC4_CREW_DETOUR(addr::cSC4NetworkConstructionCrew_MakeDustEffect, MakeDustEffect),
    OPENSC4_CREW_DETOUR(addr::cSC4NetworkConstructionCrew_ChooseWorkerPath, ChooseWorkerPath),
    OPENSC4_CREW_DETOUR(addr::cSC4NetworkConstructionCrew_PlaceWorker, PlaceWorker),
    OPENSC4_CREW_DETOUR(addr::cSC4NetworkConstructionCrew_StartWorker, StartWorker),
    OPENSC4_CREW_DETOUR(addr::cSC4NetworkConstructionCrew_AdvanceWorker, AdvanceWorker),
    OPENSC4_CREW_DETOUR(addr::cSC4NetworkConstructionCrew_AdvanceWorkerToPiece, AdvanceWorkerToPiece),
    OPENSC4_CREW_DETOUR(addr::cSC4NetworkConstructionCrew_TickWorker, TickWorker),
    OPENSC4_CREW_DETOUR(addr::cSC4NetworkConstructionCrew_StartJobWorkers, StartJobWorkers),
};

const Detour kJobEnd[] = {
    OPENSC4_CREW_DETOUR(addr::cSC4NetworkConstructionCrew_EndJob, EndJob),
};

bool InstallWorkers(registry::Mode) { return InstallDetours(kWorkers, "workers"); }
bool InstallJobEnd(registry::Mode) { return InstallDetours(kJobEnd, "jobend"); }

const bool g_registered[] = {
    registry::Register({"simulators.crew.workers", "simulators/crew", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4NetworkConstructionCrew_TickWorker,
                        "Our network construction crew workers: the direction between pieces, the path side, the "
                        "worker vehicle made, placed, started, advanced, moved each frame and dismissed, the path "
                        "choice, the dust effect, and the start of a job's workers (12 detours; "
                        "src/core/crew_workers.cpp)",
                        false, InstallWorkers}),
    registry::Register({"simulators.crew.jobend", "simulators/crew", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4NetworkConstructionCrew_EndJob,
                        "Our network construction crew job end: effect stopped, pieces finished, pedestrians "
                        "removed, workers dismissed, the job stopwatch stopped on the crew's simulation clock (1 "
                        "detour; src/core/crew_workers.cpp)",
                        false, InstallJobEnd}),
};

} // namespace
} // namespace opensc4::crew_sim
