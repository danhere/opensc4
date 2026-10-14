// Our cSC4NetworkConstructionCrew, part C2b (build 641; the class map, the
// job record and the fields are in src/core/crew_sim.h, this part's
// additions in crew_work.h): the planning and running of the jobs. Tick,
// the job's Begin and NextPiece (group simulators.crew.running); AddSegment,
// PlanJobs and Start (simulators.crew.planning). Each is a detour at the
// game's address, and every call from one of them to another function of
// the class goes through the game's address (the first part's, the workers'
// part's and this part's alike), so a group that is off, or a function on
// the skip list OPENSC4_CREW_SKIP, is the game's.
//
// Left the game's: SpawnPedestrians 0x006072E0 (1,447 bytes). Its heading
// comes from fsin and fcos of a draw times the static 0x00B43E80, the x87
// stack carries a value (the table 0x00AA74D8's entry) across the calls of
// its inner loop, it walks the piece's path objects through a hash map the
// helpers 0x00540160 and 0x004B0390 build, grows and erases its point vector
// through 0x0050C3B0 and 0x00506280, and seeds a generator on its stack
// (0x00913A5C on memory the function never constructs) for 0x0080B680. Not
// every instruction of that is certain, so it stays the game's; BeginJob and
// NextJobPiece call it at its address (so its seed site 0x006077CB is the
// game's own, and determinism.fixed_seed sees it as before).
//
// Floats: only AddSegment has any (two Manhattan distances of cells as the
// x87 computes them: each coordinate loaded as an unsigned 32-bit number,
// differences, fabs and the sum all exact in a double, truncated by the
// game's _ftol 0x009EEF04 to 64 bits, of which the low 32 count).
//
// Stopwatches: NextJobPiece starts the new piece's (sim_timers::
// CrewStopwatchStart, the game's GZStopwatch::Start 0x0089058F at
// 0x00607B4A), Start starts every job's (0x00608BF7), Tick reads them
// (sim_timers::CrewStopwatchElapsed, ElapsedMilliseconds 0x00890198 at
// 0x00607CCB). The frame time Tick hands to the workers is
// sim_timers::CrewFrameMicroseconds of the message's value.
//
// The detours are plain functions, so they clobber ECX and EDX. Every
// caller loads both before it reads them: Tick's only caller DoMessage
// 0x00607F14 (`pop edi; mov al, 1`); BeginJob's PlanJobs 0x006084B9 and
// 0x006084C5 (`lea ecx`, `mov ebp, [esi+0x4C]`) and Start 0x006089D7 (`mov
// ebx, [esi+0x4C]`); NextJobPiece's Tick 0x00607D2A (`or [esp+0x13], al`)
// and OnOccupantRemoved 0x00607E36 (`test al, al`, then `mov eax, [ebx]`):
// both read only AL; AddSegment's tool 0x0063BD09 (`lea eax`, then the
// pushes for FinishPieces); PlanJobs' Start 0x00608ADB (`lea ecx, [esp+0x54];
// lea ebp, [edi+1]`); Start's tool 0x0063BEEA (`mov edx, [ebx]; mov ecx,
// ebx`). None of them reads EAX after but those two AL readers.
//
// Calls, each count from the asm's pushes for that call and the callee's own
// `ret N`:
//   - the allocation 0x0090CF54 and the free 0x0090CF63 (cdecl, one);
//   - this class: the job's End 0x00606750 two (the job, 0 or 1; ret 8),
//     NextJobPiece 0x00607AD0 one (ret 4, AL), BeginJob 0x00607890 one (ret
//     4), SpawnPedestrians 0x006072E0 one (ret 4), PlanJobs 0x00607F30 three
//     (first, last, the list; ret 0xC), PostMessage 0x006058C0 four (type,
//     object, data 2, data 3; ret 0x10), NextPiece 0x00605BB0 four (list,
//     current*, out*, adjacent*; ret 0x10), FindPieceAtCell 0x00605C60 four
//     (out*, list, x, z; ret 0x10), SegmentStep 0x00605AC0 seven (x*, z*,
//     dx*, dz*, stepX*, stepZ*, index; ret 0x1C), ResetJob 0x00605A40 one
//     (ret 4), ConstructJob 0x00605CC0 none (plain ret), CopyJob 0x00606980
//     one (ret 4), CopyJobs 0x00606A90 (cdecl, five pushed, three read: `add
//     esp, 0x20` with DestroyJobs'), DestroyJobs 0x00606AE0 (cdecl, three),
//     FinishPieces 0x00605B30 (cdecl, two), IncrementWork 0x00605660 and
//     DecrementWork 0x006056D0 (cdecl, one), the dust effect 0x00605940 one
//     (the piece; ret 4; EAX the effect), the workers' 0x006061B0 (list,
//     piece, worker, byte), 0x006063C0 (list, piece, worker, byte) and
//     0x00605F60 (the old piece, piece, worker, byte) four each (ret 0x10),
//     0x00606500 three (microseconds, worker, job; ret 0xC), 0x006066E0 one
//     (the job; ret 4) and Dismiss 0x00605800 one (the worker; ret 4);
//   - a piece's 0x4 and 0x8 none, 0x30 GetNetworkFlag none, 0xC0 none; an
//     occupant's 0x18 none; a property holder's 0x28 four (the property, 0,
//     1, 0); SC4::GetPropertyUint32 0x005FD480 and SC4::SetPropertyUint32
//     0x005FE3C0 (cdecl, three each); the frame service (SC4::
//     GetService86AD10EC 0x00602600, cdecl, none) 0x14 two (the crew, 0xC0);
//     the crew's slot 5 (Shutdown) none;
//   - SC4Stopwatch::Destruct 0x00A6D837 none (a bare ret), and the three
//     stopwatch calls through sim_timers (above).
#include "core/crew_work.h"

#include "core/address_check.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"
#include "core/sim_timers.h"

#include <opensc4/addresses.h>

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace opensc4::crew_sim {
namespace {

bool g_tickOurs = false;

void Free(void* memory)
{
    CallCdecl<void, void*>(addr::SC4_Free, memory);
}

char* Allocate(uint32_t size)
{
    return CallCdecl<char*, uint32_t>(addr::SC4_stl_allocate, size);
}

Vector* VectorAt(char* object, std::size_t offset) { return reinterpret_cast<Vector*>(object + offset); }

// The game's inline push_back of a piece on a list: a node of 0xC bytes,
// its value stored (the game tests the node's +8 against null first, which
// is never true for an allocation), linked before the head.
void PushBack(ListNode* head, void* piece)
{
    auto* const node = reinterpret_cast<ListNode*>(Allocate(kListNodeSize));
    node->value = piece;
    ListNode* const last = head->prev;
    node->next = head;
    node->prev = last;
    last->next = node;
    head->prev = node;
}

// The inline clear of a list (crew_life.cpp's FreeNodes): every node freed,
// the head read again after each, then linked to itself.
void FreeNodes(ListNode** headAt)
{
    ListNode* node = (*headAt)->next;
    while (node != *headAt) {
        ListNode* const next = node->next;
        Free(node);
        node = next;
    }
    (*headAt)->next = *headAt;
    (*headAt)->prev = *headAt;
}

// A job on the stack (Start, PlanJobs): the game's frame holds it
// uninitialised and ConstructJob and ResetJob set every field anyone reads
// (the workers' byte +0x19, which they copy along, is set by the vehicle's
// making before it is read); ours starts zeroed.
struct alignas(8) LocalJob {
    char bytes[kJobSize];
};

// The destruction of a job on the stack, inline in Start (0x00608AE0) and
// PlanJobs (0x0060869D): the stopwatch's destructor, the list's nodes freed
// and its head linked to itself, the head freed (Start: always; PlanJobs:
// when not null), the pedestrian vector's memory freed when not null.
void DestroyLocalJob(char* job, bool testHead)
{
    CallThis<void>(addr::SC4Stopwatch_Destruct, job + job::kStopwatch);
    FreeNodes(&At<ListNode*>(job, job::kPieces));
    ListNode* const head = At<ListNode*>(job, job::kPieces);
    if (!testHead || head)
        Free(head);
    if (char* const pedestrians = At<char*>(job, job::kPedestrians))
        Free(pedestrians);
}

// The inline push_back of a job on the crew's vector (+0x48), as Start
// (0x006089DC) and PlanJobs (0x006084CA, 0x006085B3) have it: with room, the
// copy constructor at the end; else new storage for the size plus the
// larger of the size and 1 (compared unsigned), the old jobs copied over one
// by one, the new one after them, the old range destroyed (DestroyJobs, its
// third argument the address of a byte) and its memory freed.
void PushJob(char* self, char* job)
{
    Vector* const jobs = VectorAt(self, field::kJobs);
    char* const end = jobs->end;
    if (end != jobs->capacity) {
        if (end)
            CallThis<char*, char*>(addr::cSC4NetworkConstructionCrew_CopyJob, end, job);
        jobs->end += kJobSize;
        return;
    }
    const uint32_t size = static_cast<uint32_t>(static_cast<int32_t>(end - jobs->begin) >> 7);
    const uint32_t capacity = size + (size < 1 ? 1u : size);
    char* const memory = capacity ? Allocate(capacity << 7) : nullptr;
    char* to = memory;
    for (char* from = jobs->begin; from != end; from += kJobSize, to += kJobSize)
        if (to)
            CallThis<char*, char*>(addr::cSC4NetworkConstructionCrew_CopyJob, to, from);
    if (to)
        CallThis<char*, char*>(addr::cSC4NetworkConstructionCrew_CopyJob, to, job);
    uint8_t dummy = 0;
    CallCdecl<void, char*, char*, void*>(addr::cSC4NetworkConstructionCrew_DestroyJobs, jobs->begin, jobs->end,
                                         &dummy);
    if (jobs->begin)
        Free(jobs->begin);
    jobs->begin = memory;
    jobs->end = to + kJobSize;
    jobs->capacity = memory + (capacity << 7);
}

// A piece's property holder: its occupant's (slot 0xC0, then the
// occupant's slot 0x18); null when it has none.
void* HolderOf(void* piece)
{
    return Virtual<void*>(Virtual<void*>(piece, piece_slot::kAsOccupant), occupant_slot::kAsPropertyHolder);
}

uint32_t* CellAt(char* records, uint32_t index)
{
    return reinterpret_cast<uint32_t*>(records + static_cast<std::size_t>(index) * kCellRecordSize);
}

ListNode* FindPieceAtCell(char* self, ListNode** list, uint32_t x, uint32_t z)
{
    ListNode* found = nullptr;
    CallThis<ListNode**, ListNode**, ListNode**, uint32_t, uint32_t>(addr::cSC4NetworkConstructionCrew_FindPieceAtCell,
                                                                    self, &found, list, x, z);
    return found;
}

// ====================================================================================
// simulators.crew.running
// ====================================================================================

// 0x00607890, ret 4 (the job; callers PlanJobs 0x006084B9 and 0x006084C5,
// Start 0x006089D7, all on jobs on their stacks). Nothing for a job whose
// list is empty, or whose first node holds no piece (the null is stored as
// the current piece first). Else the piece becomes the current one,
// referenced; the dust effect made on it (0x00605940) and stored; the
// pedestrians (SpawnPedestrians, the game's); with the sound service the
// message 0x89F10866 (the piece's occupant, data 2 1, data 3 the piece's
// network flags: slot 0x30 asked before 0xC0); worker A started on the
// piece (0x006061B0 with the list, the piece, worker A and the byte +0x38).
// EAX is not read by any caller.
void __fastcall BeginJob(char* self, void*, char* job)
{
    static bool logged = false;
    FirstCall("BeginJob", logged);
    ListNode** const list = &At<ListNode*>(job, job::kPieces);
    ListNode* const head = *list;
    if (head->next == head)
        return;
    void* const piece = head->next->value;
    At<void*>(job, job::kPiece) = piece;
    if (!piece)
        return;
    AddRefOf(piece);
    At<void*>(job, job::kEffect) = CallThis<void*, void*>(addr::cSC4NetworkConstructionCrew_MakeDustEffect, self,
                                                           At<void*>(job, job::kPiece));
    CallThis<void, char*>(addr::cSC4NetworkConstructionCrew_SpawnPedestrians, self, job);
    if (At<void*>(self, field::kSound)) {
        void* const current = At<void*>(job, job::kPiece);
        const uint32_t flags = Virtual<uint32_t>(current, kPieceGetNetworkFlag);
        void* const occupant = Virtual<void*>(current, piece_slot::kAsOccupant);
        CallThis<uint32_t, uint32_t, void*, uint32_t, uint32_t>(addr::cSC4NetworkConstructionCrew_PostMessage, self,
                                                                kMsgJobStarted, occupant, 1u, flags);
    }
    CallThis<void, ListNode**, void*, char*, uint32_t>(addr::cSC4NetworkConstructionCrew_StartWorker, self, list,
                                                       At<void*>(job, job::kPiece), job + job::kWorkerA,
                                                       uint32_t{At<uint8_t>(job, job::kFlag38)});
}

// 0x00607AD0, ret 4 (the job), AL the answer: whether the job goes on. The
// current piece is held (referenced) for the call; the job's End with 0;
// then NextPiece (0x00605BB0) for the piece after it in the job's list (its
// out piece referenced by NextPiece; whether the two cells are one apart).
// With a next piece: the old current released and the next one current, its
// dust effect, the pedestrians, the stopwatch started, the job's time per
// piece from the crew's +0x4, the workers started (0x006066E0); worker B
// moved on (0x006063C0 when it has a vehicle, else put on the path from the
// old piece, 0x00605F60, and marked arrived) when the cells are one apart,
// else dismissed; with the sound service the message 0xA9F10E45 (the
// piece's occupant, data 2 0, data 3 its network flags); AL 1. Without one:
// the message 0x89F10866 with no object, 0 and the old piece's flags (0
// without an old piece) when the crew has the sound service, End with 1,
// state -1, AL 0. The held piece released last.
uint8_t __fastcall NextJobPiece(char* self, void*, char* job)
{
    static bool logged = false;
    FirstCall("NextJobPiece", logged);
    void* const old = At<void*>(job, job::kPiece);
    if (old)
        AddRefOf(old);
    CallThis<void, char*, uint32_t>(addr::cSC4NetworkConstructionCrew_EndJob, self, job, 0u);
    void* current = old;
    void* next = nullptr;
    uint8_t adjacent = 0;
    ListNode** const list = &At<ListNode*>(job, job::kPieces);
    uint8_t goesOn = 0;
    if (CallThis<uint8_t, ListNode**, void**, void**, uint8_t*>(addr::cSC4NetworkConstructionCrew_NextPiece, self,
                                                               list, &current, &next, &adjacent)) {
        if (void* const held = At<void*>(job, job::kPiece))
            ReleaseOf(held);
        At<void*>(job, job::kPiece) = next;
        At<void*>(job, job::kEffect) =
            CallThis<void*, void*>(addr::cSC4NetworkConstructionCrew_MakeDustEffect, self, next);
        CallThis<void, char*>(addr::cSC4NetworkConstructionCrew_SpawnPedestrians, self, job);
        sim_timers::CrewStopwatchStart(job + job::kStopwatch);
        At<uint32_t>(job, job::kMilliseconds) = At<uint32_t>(self, field::kPieceMilliseconds);
        CallThis<void, char*>(addr::cSC4NetworkConstructionCrew_StartJobWorkers, self, job);
        if (adjacent) {
            const uint32_t flag = At<uint8_t>(job, job::kFlag38);
            void* const piece = At<void*>(job, job::kPiece);
            if (At<void*>(job, job::kWorkerB + worker::kVehicle)) {
                CallThis<void, ListNode**, void*, char*, uint32_t>(
                    addr::cSC4NetworkConstructionCrew_AdvanceWorkerToPiece, self, list, piece, job + job::kWorkerB,
                    flag);
            } else {
                CallThis<void, void*, void*, char*, uint32_t>(addr::cSC4NetworkConstructionCrew_PlaceWorker, self, old,
                                                              piece, job + job::kWorkerB, flag);
                At<uint8_t>(job, job::kWorkerB + worker::kArrived) = 1;
            }
        } else {
            CallThis<void, char*>(addr::cSC4NetworkConstructionCrew_DismissWorker, self, job + job::kWorkerB);
        }
        if (At<void*>(self, field::kSound)) {
            void* const piece = At<void*>(job, job::kPiece);
            const uint32_t flags = Virtual<uint32_t>(piece, kPieceGetNetworkFlag);
            void* const occupant = Virtual<void*>(piece, piece_slot::kAsOccupant);
            CallThis<uint32_t, uint32_t, void*, uint32_t, uint32_t>(addr::cSC4NetworkConstructionCrew_PostMessage,
                                                                    self, kMsgPieceDone, occupant, 0u, flags);
        }
        goesOn = 1;
    } else {
        uint32_t flags = 0;
        if (old)
            flags = Virtual<uint32_t>(old, kPieceGetNetworkFlag);
        if (At<void*>(self, field::kSound))
            CallThis<uint32_t, uint32_t, void*, uint32_t, uint32_t>(addr::cSC4NetworkConstructionCrew_PostMessage,
                                                                    self, kMsgJobStarted, nullptr, 0u, flags);
        CallThis<void, char*, uint32_t>(addr::cSC4NetworkConstructionCrew_EndJob, self, job, 1u);
        At<int32_t>(job, job::kState) = -1;
    }
    if (old)
        ReleaseOf(old);
    return goesOn;
}

// 0x00607C30, ret 4 (the frame message's microseconds; only caller
// DoMessage 0x00607F14). The frame time is sim_timers::CrewFrameMicroseconds
// of it (the message's own value when determinism.sim_timers is off). No
// jobs: the crew's slot 5 (Shutdown). Else for every job (the count taken
// again after each, compared unsigned; the vector's begin read again) whose
// state is above 0 (signed) and whose time per piece is not 0: each worker
// with a vehicle that has not arrived moved by the frame time (0x00606500
// with the time, the worker, the job) and counted as on its way when it has
// still not arrived; the job's stopwatch read; when it shows the time per
// piece (twice that while a worker is on its way; both unsigned, the double
// a 32-bit shift) the piece's work property taken down (DecrementWork). Then
// the work property read (0 without a holder or without the property): at
// 0 the job's NextPiece, whose AL counts as work; else there is work, and
// worker A's StartJobWorkers when it has arrived on the current piece. No
// work in any job: Shutdown (which can end in the crew's destruction:
// nothing touches the crew after it).
void __fastcall Tick(char* self, void*, uint32_t microseconds)
{
    static bool logged = false;
    FirstCall("Tick", logged);
    const uint32_t frame = sim_timers::CrewFrameMicroseconds(microseconds);
    Vector* const jobs = VectorAt(self, field::kJobs);
    auto jobCount = [jobs] { return static_cast<uint32_t>(static_cast<int32_t>(jobs->end - jobs->begin) >> 7); };
    uint8_t working = 0;
    if (jobCount() != 0) {
        uint32_t index = 0, offset = 0;
        do {
            char* const job = jobs->begin + offset;
            if (At<int32_t>(job, job::kState) > 0 && At<uint32_t>(job, job::kMilliseconds) != 0) {
                uint8_t onTheWay = 0;
                char* const a = job + job::kWorkerA;
                char* const b = job + job::kWorkerB;
                if (At<void*>(a, worker::kVehicle) && !At<uint8_t>(a, worker::kArrived)) {
                    CallThis<void, uint32_t, char*, char*>(addr::cSC4NetworkConstructionCrew_TickWorker, self, frame, a,
                                                           job);
                    onTheWay = At<uint8_t>(a, worker::kArrived) == 0;
                }
                if (At<void*>(b, worker::kVehicle) && !At<uint8_t>(b, worker::kArrived)) {
                    CallThis<void, uint32_t, char*, char*>(addr::cSC4NetworkConstructionCrew_TickWorker, self, frame, b,
                                                           job);
                    onTheWay |= static_cast<uint8_t>(At<uint8_t>(b, worker::kArrived) == 0);
                }
                const uint32_t elapsed = sim_timers::CrewStopwatchElapsed(job + job::kStopwatch);
                const uint32_t perPiece = At<uint32_t>(job, job::kMilliseconds);
                if (elapsed >= (onTheWay ? perPiece << 1 : perPiece))
                    CallCdecl<uint32_t, void*>(addr::cSC4NetworkConstructionCrew_DecrementWork,
                                               At<void*>(job, job::kPiece));
                uint32_t work = 0;
                if (void* const holder = HolderOf(At<void*>(job, job::kPiece)))
                    CallCdecl<uint8_t, void*, uint32_t, uint32_t*>(addr::SC4_GetPropertyUint32, holder, kWorkProperty,
                                                                   &work);
                if (work == 0) {
                    working |= CallThis<uint8_t, char*>(addr::cSC4NetworkConstructionCrew_NextJobPiece, self, job);
                } else {
                    working = 1;
                    if (At<uint8_t>(a, worker::kArrived) && At<void*>(job, job::kPiece) == At<void*>(a, worker::kPiece))
                        CallThis<void, char*>(addr::cSC4NetworkConstructionCrew_StartJobWorkers, self, job);
                }
            }
            offset += kJobSize;
            ++index;
        } while (index < jobCount());
        if (working)
            return;
    }
    Virtual<uint8_t>(self, slot::kShutdown);
}

// ====================================================================================
// simulators.crew.planning
// ====================================================================================

// The Manhattan distance of two cell records as AddSegment computes it on
// the x87 (0x00606C9C to 0x00606D0B and 0x00606D10 to 0x00606D66): each
// coordinate loaded as a signed 32-bit integer plus 2^32 (the float at
// 0x00A80AA8) when negative, that is its unsigned value; |a.z - b.z| plus
// |a.x - b.x|, all exact in a double; then _ftol 0x009EEF04 (truncation to
// 64 bits) whose low 32 bits are the answer.
int32_t CellDistance(const uint32_t* a, const uint32_t* b)
{
    const double dz = std::fabs(static_cast<double>(a[1]) - static_cast<double>(b[1]));
    const double dx = std::fabs(static_cast<double>(a[0]) - static_cast<double>(b[0]));
    return static_cast<int32_t>(static_cast<int64_t>(dz + dx));
}

// 0x00606B50, ret 0xC: AddSegment(pieces, segments, cells), only caller the
// network tool's 0x0063BD09: the address of the list (head pointer) of the
// segment's new pieces, the tool's vector of 12-byte segment records (+0x54)
// and of 8-byte cell records (+0x60). No segment records and a piece list
// that is not empty: FinishPieces(pieces, 0) and nothing else. Otherwise,
// with the count of the crew's cell records before the call: for each
// segment record, whether the crew already has a segment with the same end
// cells and third word; its cells appended to the crew's cell records from
// the end nearer the tool's FIRST segment record (the distance of its first
// cell to that record's first cell against the distance of its last cell to
// that record's last cell: the last cell first when that is smaller); and,
// when it is new, its two indices raised by the count (in the tool's own
// vector) and the record appended to the crew's segments. Then for every
// cell record of the crew, a new piece on that cell moves from the given
// list to the crew's (in place of the crew's own piece on the cell, which is
// released, or at the end); last FinishPieces(pieces, 0) for the pieces
// left in the given list. The appends grow as the game's vectors do.
void __fastcall AddSegment(char* self, void*, ListNode** pieces, Vector* segments, Vector* cells)
{
    static bool logged = false;
    FirstCall("AddSegment", logged);
    if (segments->begin == segments->end) {
        ListNode* const head = *pieces;
        if (head->next != head) {
            CallCdecl<void, ListNode**, uint32_t>(addr::cSC4NetworkConstructionCrew_FinishPieces, pieces, 0u);
            return;
        }
    }
    Vector* const records = VectorAt(self, field::kRecords);
    Vector* const crewSegments = VectorAt(self, field::kSegments);
    const uint32_t base = static_cast<uint32_t>(static_cast<int32_t>(records->end - records->begin) >> 3);
    int32_t left = static_cast<int32_t>(segments->end - segments->begin) / static_cast<int32_t>(segment::kSize);
    if (left > 0) {
        uint32_t offset = 0;
        do {
            auto* const record = reinterpret_cast<uint32_t*>(segments->begin + offset);
            const uint32_t first = record[0];
            const int32_t known =
                static_cast<int32_t>(crewSegments->end - crewSegments->begin) / static_cast<int32_t>(segment::kSize);
            char* const cellBase = cells->begin;
            const uint32_t last = record[1];
            uint8_t duplicate = 0;
            for (int32_t j = 0; j < known; ++j) {
                const auto* const other = reinterpret_cast<const uint32_t*>(crewSegments->begin + j * segment::kSize);
                const uint32_t* const otherFirst = CellAt(records->begin, other[0]);
                const uint32_t* const otherLast = CellAt(records->begin, other[1]);
                const uint32_t* const cellFirst = CellAt(cellBase, first);
                const uint32_t* const cellLast = CellAt(cellBase, last);
                if (otherFirst[0] == cellFirst[0] && otherFirst[1] == cellFirst[1] && otherLast[0] == cellLast[0] &&
                    otherLast[1] == cellLast[1] && other[2] == record[2]) {
                    duplicate = 1;
                    break;
                }
            }
            const auto* const toolFirst = reinterpret_cast<const uint32_t*>(segments->begin);
            const int32_t toFirst = CellDistance(CellAt(cellBase, first), CellAt(cellBase, toolFirst[0]));
            const int32_t toLast = CellDistance(CellAt(cellBase, last), CellAt(cellBase, toolFirst[1]));
            uint32_t at = first, stop = last + 1;
            int32_t step = 1;
            if (toLast < toFirst) {
                at = last;
                stop = first - 1;
                step = -1;
            }
            for (; at != stop; at += static_cast<uint32_t>(step)) {
                const uint32_t* const cell = CellAt(cells->begin, at);
                char* const end = records->end;
                if (end != records->capacity) {
                    if (end) {
                        reinterpret_cast<uint32_t*>(end)[0] = cell[0];
                        reinterpret_cast<uint32_t*>(end)[1] = cell[1];
                    }
                    records->end += kCellRecordSize;
                    continue;
                }
                const uint32_t size = static_cast<uint32_t>(static_cast<int32_t>(end - records->begin) >> 3);
                const uint32_t capacity = size + (size < 1 ? 1u : size);
                char* const memory = capacity ? Allocate(capacity * kCellRecordSize) : nullptr;
                char* to = memory;
                for (char* from = records->begin; from != end; from += kCellRecordSize, to += kCellRecordSize)
                    if (to) {
                        reinterpret_cast<uint32_t*>(to)[0] = reinterpret_cast<uint32_t*>(from)[0];
                        reinterpret_cast<uint32_t*>(to)[1] = reinterpret_cast<uint32_t*>(from)[1];
                    }
                if (to) {
                    reinterpret_cast<uint32_t*>(to)[0] = cell[0];
                    reinterpret_cast<uint32_t*>(to)[1] = cell[1];
                }
                if (records->begin)
                    Free(records->begin);
                records->capacity = memory + capacity * kCellRecordSize;
                records->begin = memory;
                records->end = to + kCellRecordSize;
            }
            if (!duplicate) {
                record[0] += base;
                record[1] += base;
                char* const end = crewSegments->end;
                if (end != crewSegments->capacity) {
                    if (end)
                        for (std::size_t i = 0; i < 3; ++i)
                            reinterpret_cast<uint32_t*>(end)[i] = record[i];
                    crewSegments->end += segment::kSize;
                } else {
                    const uint32_t size = static_cast<uint32_t>(static_cast<int32_t>(end - crewSegments->begin) /
                                                                static_cast<int32_t>(segment::kSize));
                    const uint32_t capacity = size + (size < 1 ? 1u : size);
                    char* const memory = capacity ? Allocate(capacity * segment::kSize) : nullptr;
                    char* to = memory;
                    for (char* from = crewSegments->begin; from != end; from += segment::kSize, to += segment::kSize)
                        if (to)
                            for (std::size_t i = 0; i < 3; ++i)
                                reinterpret_cast<uint32_t*>(to)[i] = reinterpret_cast<uint32_t*>(from)[i];
                    if (to)
                        for (std::size_t i = 0; i < 3; ++i)
                            reinterpret_cast<uint32_t*>(to)[i] = record[i];
                    if (crewSegments->begin)
                        Free(crewSegments->begin);
                    crewSegments->begin = memory;
                    crewSegments->end = to + segment::kSize;
                    crewSegments->capacity = memory + capacity * segment::kSize;
                }
            }
            offset += segment::kSize;
        } while (--left != 0);
    }
    const int32_t count = static_cast<int32_t>(records->end - records->begin) >> 3;
    ListNode** const crewList = &At<ListNode*>(self, field::kPieces);
    for (int32_t i = 0; i < count; ++i) {
        const uint32_t* cell = CellAt(records->begin, static_cast<uint32_t>(i));
        ListNode* const node = FindPieceAtCell(self, pieces, cell[0], cell[1]);
        if (node == *pieces)
            continue;
        cell = CellAt(records->begin, static_cast<uint32_t>(i));
        ListNode* const where = FindPieceAtCell(self, crewList, cell[0], cell[1]);
        ListNode* const head = *crewList;
        auto* const fresh = reinterpret_cast<ListNode*>(Allocate(kListNodeSize));
        fresh->value = node->value; // the game's test of the node's +8 against null, never true, left out
        ListNode* const before = where->prev;
        fresh->next = where;
        fresh->prev = before;
        before->next = fresh;
        where->prev = fresh;
        if (where != head) {
            ReleaseOf(where->value);
            where->prev->next = where->next;
            where->next->prev = where->prev;
            Free(where);
        }
        node->prev->next = node->next;
        node->next->prev = node->prev;
        Free(node);
    }
    CallCdecl<void, ListNode**, uint32_t>(addr::cSC4NetworkConstructionCrew_FinishPieces, pieces, 0u);
}

// The step PlanJobs makes inline for each cursor after a pass without a
// piece (0x0060828A to 0x006082F1, 0x006082F5 to 0x00608354): at the end
// cell, true and no step; else one step as SegmentStep's (x by dx when the
// x byte is set, else z by dz when the z byte is set; then both bytes
// flipped) and false.
uint8_t StepCursor(const uint32_t* end, uint32_t& x, uint32_t& z, uint32_t dx, uint32_t dz, uint8_t& stepX,
                   uint8_t& stepZ)
{
    if (x == end[0] && z == end[1])
        return 1;
    if (stepX)
        x += dx;
    else if (stepZ)
        z += dz;
    stepX = stepX == 0;
    stepZ = stepZ == 0;
    return 0;
}

// 0x00607F30, ret 0xC: PlanJobs(first, last, list), the segment records
// first to last and the crew's piece list; only caller Start 0x00608ADB
// (crews whose +0xC is not 1). EAX last + 1 (Start does not read it). Two
// jobs on the stack (constructed, the second's after the first's, then
// ResetJob each), and the direction from the first cell of segment `first`
// to the first cell of segment `last`: each of dx and dz divided by its own
// absolute value when not 0.
//  - Both not 0: two cursors start on one cell (segment `first`'s last cell
//    when dx is negative, else its first) and run to segment `last`'s last
//    cell, the first stepping x first, the second z first. A pass looks over
//    the segments' cells in order for the first cell with a piece in the list
//    that a cursor is on: the piece goes (referenced, IncrementWork) to the
//    first job for the first cursor and to the second job for the second,
//    the cursor steps (SegmentStep, which says whether it was at the end),
//    and the node leaves the list (the list's reference released). A pass
//    that finds nothing steps both cursors inline and ends the planning; so
//    does the first cursor's end, and the second's at the next pass.
//  - Otherwise: per segment, the piece on its first cell (if in the list) to
//    the first job and the one on its last cell to the second, each with
//    IncrementWork and no reference of its own (the list's is given over),
//    and then the list's nodes freed.
// Each job's state counts its pieces. Then each job's byte +0x38 cleared,
// both begun (BeginJob), pushed on the crew's job vector in that order, and
// the two on the stack destroyed (the second first).
uint32_t __fastcall PlanJobs(char* self, void*, int32_t first, int32_t last, ListNode** list)
{
    static bool logged = false;
    FirstCall("PlanJobs", logged);
    LocalJob one = {}, two = {};
    char* const job1 = one.bytes;
    char* const job2 = two.bytes;
    CallThis<char*>(addr::cSC4NetworkConstructionCrew_ConstructJob, job1);
    CallThis<char*>(addr::cSC4NetworkConstructionCrew_ConstructJob, job2);
    uint8_t stepX1 = 1, stepZ1 = 0, stepX2 = 0, stepZ2 = 1;
    CallThis<uint8_t, char*>(addr::cSC4NetworkConstructionCrew_ResetJob, self, job1);
    CallThis<uint8_t, char*>(addr::cSC4NetworkConstructionCrew_ResetJob, self, job2);

    Vector* const segments = VectorAt(self, field::kSegments);
    Vector* const records = VectorAt(self, field::kRecords);
    auto segmentAt = [segments](int32_t index) {
        return reinterpret_cast<uint32_t*>(segments->begin + static_cast<std::size_t>(index) * segment::kSize);
    };
    const uint32_t* const from = CellAt(records->begin, segmentAt(first)[0]);
    const uint32_t* const to = CellAt(records->begin, segmentAt(last)[0]);
    const int32_t dx = static_cast<int32_t>(to[0] - from[0]);
    const int32_t dz = static_cast<int32_t>(to[1] - from[1]);
    auto magnitude = [](int32_t value) {
        const int32_t sign = value >> 31;
        return (value ^ sign) - sign;
    };
    int32_t sx = dx, sz = dz;
    if (dx != 0)
        sx = dx / magnitude(dx);
    if (dz != 0)
        sz = dz / magnitude(dz);
    uint32_t dirX = static_cast<uint32_t>(sx), dirZ = static_cast<uint32_t>(sz); // SegmentStep's dx and dz

    if (sx != 0 && sz != 0) {
        const uint32_t endIndex = segmentAt(last)[1];
        const uint32_t startIndex = sx < 0 ? segmentAt(first)[1] : segmentAt(first)[0];
        const uint32_t* const start = CellAt(records->begin, startIndex);
        uint32_t x1 = start[0], z1 = start[1], x2 = start[0], z2 = start[1];
        uint8_t done1 = 0, done2 = 0, found = 1;
        for (;;) {
            if (done2 || !found)
                break;
            found = 0;
            for (int32_t index = first; index <= last && !found; ++index) {
                uint32_t cell = segmentAt(index)[0];
                if (cell > segmentAt(index)[1])
                    continue;
                do {
                    if (found)
                        break;
                    const uint32_t* const at = CellAt(records->begin, cell);
                    ListNode* const node = FindPieceAtCell(self, list, at[0], at[1]);
                    if (node != *list) {
                        void* const piece = node->value;
                        const uint32_t* here = CellAt(records->begin, cell);
                        if (here[0] == x1 && here[1] == z1) {
                            AddRefOf(piece);
                            CallCdecl<uint32_t, void*>(addr::cSC4NetworkConstructionCrew_IncrementWork, piece);
                            PushBack(At<ListNode*>(job1, job::kPieces), piece);
                            ++At<int32_t>(job1, job::kState);
                            found = 1;
                            done1 = CallThis<uint8_t, uint32_t*, uint32_t*, uint32_t*, uint32_t*, uint8_t*, uint8_t*,
                                             uint32_t>(addr::cSC4NetworkConstructionCrew_SegmentStep, self, &x1, &z1,
                                                       &dirX, &dirZ, &stepX1, &stepZ1, endIndex);
                        }
                        here = CellAt(records->begin, cell);
                        if (here[0] == x2 && here[1] == z2) {
                            AddRefOf(piece);
                            CallCdecl<uint32_t, void*>(addr::cSC4NetworkConstructionCrew_IncrementWork, piece);
                            PushBack(At<ListNode*>(job2, job::kPieces), piece);
                            ++At<int32_t>(job2, job::kState);
                            found = 1;
                            done2 = CallThis<uint8_t, uint32_t*, uint32_t*, uint32_t*, uint32_t*, uint8_t*, uint8_t*,
                                             uint32_t>(addr::cSC4NetworkConstructionCrew_SegmentStep, self, &x2, &z2,
                                                       &dirX, &dirZ, &stepX2, &stepZ2, endIndex);
                        }
                        if (found) {
                            ReleaseOf(piece);
                            node->prev->next = node->next;
                            node->next->prev = node->prev;
                            Free(node);
                        }
                    }
                    ++cell;
                } while (cell <= segmentAt(index)[1]);
            }
            if (!found && !(done1 && done2)) {
                const uint32_t* const end = CellAt(records->begin, endIndex);
                done1 = StepCursor(end, x1, z1, dirX, dirZ, stepX1, stepZ1);
                done2 = StepCursor(end, x2, z2, dirX, dirZ, stepX2, stepZ2);
            }
            if (done1)
                break;
        }
    } else {
        if (first <= last) {
            uint32_t left = static_cast<uint32_t>(last - first + 1);
            int32_t index = first;
            do {
                const uint32_t* cell = CellAt(records->begin, segmentAt(index)[0]);
                ListNode* node = FindPieceAtCell(self, list, cell[0], cell[1]);
                if (node != *list) {
                    CallCdecl<uint32_t, void*>(addr::cSC4NetworkConstructionCrew_IncrementWork, node->value);
                    PushBack(At<ListNode*>(job1, job::kPieces), node->value);
                    ++At<int32_t>(job1, job::kState);
                }
                cell = CellAt(records->begin, segmentAt(index)[1]);
                node = FindPieceAtCell(self, list, cell[0], cell[1]);
                if (node != *list) {
                    CallCdecl<uint32_t, void*>(addr::cSC4NetworkConstructionCrew_IncrementWork, node->value);
                    PushBack(At<ListNode*>(job2, job::kPieces), node->value);
                    ++At<int32_t>(job2, job::kState);
                }
                ++index;
            } while (--left != 0);
        }
        FreeNodes(list);
    }

    At<uint8_t>(job1, job::kFlag38) = 0;
    At<uint8_t>(job2, job::kFlag38) = 0;
    CallThis<void, char*>(addr::cSC4NetworkConstructionCrew_BeginJob, self, job1);
    CallThis<void, char*>(addr::cSC4NetworkConstructionCrew_BeginJob, self, job2);
    PushJob(self, job1);
    PushJob(self, job2);
    DestroyLocalJob(job2, true);
    DestroyLocalJob(job1, true);
    return static_cast<uint32_t>(last + 1);
}

// 0x00608770, no arguments (`ret`), only caller the network tool's
// 0x0063BEEA. Fewer than 5 segment records: FinishPieces(list, 0) and the
// crew's slot 5 (Shutdown), and nothing else. Otherwise, with n records:
// the job count max(1, (2n) / (n + 1)) and per job max(1, n / count)
// records, the remainder spread one by one over the first jobs (as the
// game computes it, with n + 1 where a job's span is tested: every job
// reaches the last record); the job vector emptied (CopyJobs of the empty
// tail, DestroyJobs of the rest). For each job a job on the stack
// (ConstructJob, ResetJob): with the crew's +0xC at 1 the job's state is
// the sum of the cells of its records, its byte +0x38 cleared, it takes the
// first min(pieces in the crew's list, that sum) pieces off the list (each
// node freed, the piece's work property raised as IncrementWork does,
// inline), BeginJob, and it is pushed on the vector; with any other +0xC
// PlanJobs does the job's records. The job on the stack destroyed. Then the
// crew joins the frame service (code 0xC0; +0x15 set first) unless it is
// in, the pieces left in the list finished and released (FinishPieces with
// 1; the nodes stay), the segment and cell records emptied, and every job's
// stopwatch started.
void __fastcall Start(char* self, void*)
{
    static bool logged = false;
    FirstCall("Start", logged);
    Vector* const segments = VectorAt(self, field::kSegments);
    ListNode** const list = &At<ListNode*>(self, field::kPieces);
    const int32_t n = static_cast<int32_t>(segments->end - segments->begin) / static_cast<int32_t>(segment::kSize);
    const int32_t span = n + 1;
    if (n < 5) {
        CallCdecl<void, ListNode**, uint32_t>(addr::cSC4NetworkConstructionCrew_FinishPieces, list, 0u);
        Virtual<uint8_t>(self, slot::kShutdown);
        return;
    }
    const int32_t wanted = (n + span - 1) / span;
    const int32_t jobCount = wanted < 1 ? 1 : wanted;
    const int32_t quotient = n / jobCount;
    int32_t remainder = n % jobCount;
    const int32_t perJob = quotient < 1 ? 1 : quotient;
    Vector* const jobs = VectorAt(self, field::kJobs);
    uint8_t dummy = 0;
    char* const kept = CallCdecl<char*, char*, char*, char*, void*, uint32_t>(
        addr::cSC4NetworkConstructionCrew_CopyJobs, jobs->end, jobs->end, jobs->begin, &dummy, 0u);
    CallCdecl<void, char*, char*, void*>(addr::cSC4NetworkConstructionCrew_DestroyJobs, kept, jobs->end, &dummy);
    jobs->end = kept;
    int32_t firstSegment = 0;
    if (jobCount > 0) {
        int32_t left = jobCount;
        do {
            int32_t lastSegment;
            if (span + firstSegment >= n) {
                lastSegment = n - 1;
            } else {
                lastSegment = perJob + firstSegment - 1;
                if (remainder != 0) {
                    ++lastSegment;
                    --remainder;
                }
            }
            LocalJob local = {};
            char* const job = local.bytes;
            CallThis<char*>(addr::cSC4NetworkConstructionCrew_ConstructJob, job);
            CallThis<uint8_t, char*>(addr::cSC4NetworkConstructionCrew_ResetJob, self, job);
            if (At<uint32_t>(self, field::kCount) == 1) {
                uint32_t cellsOfJob = At<uint32_t>(job, job::kState);
                for (int32_t index = firstSegment; index <= lastSegment; ++index) {
                    const auto* const record =
                        reinterpret_cast<const uint32_t*>(segments->begin + index * segment::kSize);
                    cellsOfJob = cellsOfJob + (record[1] - record[0]) + 1;
                    At<uint32_t>(job, job::kState) = cellsOfJob;
                }
                At<uint8_t>(job, job::kFlag38) = 0;
                ListNode* const crewHead = *list;
                int32_t inList = 0;
                for (ListNode* node = crewHead->next; node != crewHead; node = node->next)
                    ++inList;
                At<int32_t>(job, job::kState) =
                    inList < static_cast<int32_t>(cellsOfJob) ? inList : At<int32_t>(job, job::kState);
                for (int32_t taken = 0; taken < At<int32_t>(job, job::kState); ++taken) {
                    ListNode* const node = (*list)->next;
                    ListNode* const next = node->next;
                    ListNode* const previous = node->prev;
                    void* const piece = node->value;
                    previous->next = next;
                    next->prev = previous;
                    Free(node);
                    uint32_t value = 0;
                    if (void* const holder = HolderOf(piece)) {
                        if (!CallCdecl<uint8_t, void*, uint32_t, uint32_t*>(addr::SC4_GetPropertyUint32, holder,
                                                                            kWorkProperty, &value))
                            Virtual<uint8_t>(holder, kHolderAddProperty, kWorkProperty, 0u, 1u, 0u);
                        CallCdecl<void, void*, uint32_t, uint32_t>(addr::SC4_SetPropertyUint32, holder, kWorkProperty,
                                                                   value + 1);
                    }
                    PushBack(At<ListNode*>(job, job::kPieces), piece);
                }
                CallThis<void, char*>(addr::cSC4NetworkConstructionCrew_BeginJob, self, job);
                PushJob(self, job);
            } else {
                CallThis<uint32_t, int32_t, int32_t, ListNode**>(addr::cSC4NetworkConstructionCrew_PlanJobs, self,
                                                                 firstSegment, lastSegment, list);
            }
            DestroyLocalJob(job, false);
            firstSegment = lastSegment + 1;
        } while (--left != 0);
    }
    if (!At<uint8_t>(self, field::kInFrameService))
        if (void* const frames = CallCdecl<void*>(addr::SC4_GetService86AD10EC)) {
            At<uint8_t>(self, field::kInFrameService) = 1;
            Virtual<uint8_t>(frames, frame_slot::kAdd, static_cast<void*>(self), kFrameServiceCode);
        }
    CallCdecl<void, ListNode**, uint32_t>(addr::cSC4NetworkConstructionCrew_FinishPieces, list, 1u);
    segments->end = segments->begin;
    Vector* const records = VectorAt(self, field::kRecords);
    records->end = records->begin;
    auto jobCountNow = [jobs] { return static_cast<uint32_t>(static_cast<int32_t>(jobs->end - jobs->begin) >> 7); };
    if (jobCountNow() != 0) {
        uint32_t index = 0, offset = 0;
        do {
            sim_timers::CrewStopwatchStart(jobs->begin + offset + job::kStopwatch);
            ++index;
            offset += kJobSize;
        } while (index < jobCountNow());
    }
}

// ====================================================================================
// Registration
// ====================================================================================

// Tick has its own install (InstallRunning): determinism.sim_timers hooks
// the game's Tick when ours is not there.
const Detour kRunning[] = {
    OPENSC4_CREW_DETOUR(addr::cSC4NetworkConstructionCrew_BeginJob, BeginJob),
    OPENSC4_CREW_DETOUR(addr::cSC4NetworkConstructionCrew_NextJobPiece, NextJobPiece),
};

const Detour kPlanning[] = {
    OPENSC4_CREW_DETOUR(addr::cSC4NetworkConstructionCrew_AddSegment, AddSegment),
    OPENSC4_CREW_DETOUR(addr::cSC4NetworkConstructionCrew_PlanJobs, PlanJobs),
    OPENSC4_CREW_DETOUR(addr::cSC4NetworkConstructionCrew_Start, Start),
};

// simulators.crew.running installs at PreAppInit, before determinism.
// sim_timers (PostAppInit) asks TickIsOurs. Tick goes in unless the skip
// list names it or a detour is already at its entry (another order of the
// phases than today's: ours then stays out and sim_timers' hook keeps the
// game's Tick); either way the group's other detours go in.
bool InstallRunning(registry::Mode)
{
    if (!InstallDetours(kRunning, "running"))
        return false;
    const addr::Entry* const tick = addr_check::Find(addr::cSC4NetworkConstructionCrew_Tick);
    if (!tick)
        return false;
    if (Skipped(tick->address)) {
        log::Write("crew running: Tick on the skip list: the game's");
        return true;
    }
    if (hook::IsInstalledAt(tick->address)) {
        log::Write("crew running: Tick is already hooked (determinism.sim_timers?); ours not installed there");
        return true;
    }
    void* original = nullptr;
    g_tickOurs = hook::Install(*tick, reinterpret_cast<void*>(&Tick), &original);
    log::Write("crew running: Tick %s", g_tickOurs ? "is ours" : "NOT installed (hook failed)");
    return g_tickOurs;
}

bool InstallPlanning(registry::Mode) { return InstallDetours(kPlanning, "planning"); }

const bool g_registered[] = {
    registry::Register({"simulators.crew.running", "simulators/crew", registry::Kind::Detour,
                        registry::Phase::PreAppInit, // before determinism.sim_timers asks TickIsOurs (crew_work.h)
                        addr::cSC4NetworkConstructionCrew_Tick,
                        "Our network construction crew running: Tick (with determinism.sim_timers' frame time and "
                        "stopwatches), the job's Begin and NextPiece (3 detours; src/core/crew_work.cpp)",
                        false, InstallRunning}),
    registry::Register({"simulators.crew.planning", "simulators/crew", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4NetworkConstructionCrew_Start,
                        "Our network construction crew planning: AddSegment, PlanJobs and Start (3 detours; "
                        "src/core/crew_work.cpp)",
                        false, InstallPlanning}),
};

} // namespace

bool TickIsOurs() { return g_tickOurs; }

} // namespace opensc4::crew_sim
