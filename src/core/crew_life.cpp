// Our cSC4NetworkConstructionCrew, first part (C1, build 641; the class map,
// the job record, the vtable and the fields are in src/core/crew_sim.h):
// state and life. The constructor, the destructor, Release, Init, Shutdown
// and the static pair (group simulators.crew.life); DoMessage,
// OnOccupantRemoved and PostMessage (simulators.crew.messages); FinishPiece,
// FinishPieces, the two work property counters, and the job record's and
// the piece list's helpers (simulators.crew.pieces). Each is a detour at the
// game's address, and every call from one of them to another function of
// the class goes through the game's address, so a group that is off, or a
// function on the skip list OPENSC4_CREW_SKIP, is the game's. Tick
// (0x00607C30) is called at its address, where determinism.sim_timers hooks
// it; the second part's functions (the job's End 0x00606750 and NextPiece
// 0x00607AD0, the worker's Dismiss 0x00605800) too.
//
// game.quiet.network (src/core/game_quiet.cpp) wrapped the game's Init; with
// our Init at that entry it does not hook but hands us an observer
// (crew_sim.h, SetQuietObserver), and our Init withholds the sound service
// itself. The plugin sc4-disable-network-construction-sounds writes 0xEB
// over the `je` at 0x006071FC inside the game's Init; with our Init that
// byte is in code that no longer runs, so our Init reads it at every call
// and, when it is 0xEB, skips the store as the patched game does. Each of
// the three outcomes (stored, withheld by the switch, skipped by the
// plugin's byte) is logged the first time it happens.
//
// Every function was written from the disassembly. The detours are plain
// functions, so they clobber ECX and EDX. The game's functions that keep
// both on some path: StaticInit 0x00605620 and StaticShutdown 0x00605640
// (always); FinishPiece 0x00605870 for a null piece; FinishPieces 0x00605B30
// for a null list or an empty one; CopyJobs 0x00606A90 for an empty range;
// DestroyJobs 0x00606AE0 for an empty range; Shutdown 0x00607920 for a crew
// that is not initialised (its `push ecx` only makes room for a local and
// `pop ecx` puts ECX back). Their callers: Init's 0x006071A1 (`mov al,
// [esi+0x14]`, then a call) for StaticInit; nothing calls slot 7; FinishPiece
// is called only with a piece (0x00606192 and 0x0060677C test it first);
// FinishPieces from 0x0060681F, 0x00606B78, 0x006070A4, 0x006079BE,
// 0x006087C5, 0x00608B68 and the tools' 0x0062BC71, 0x0062D3FF, 0x0063BD1A,
// each loading what it reads next (`mov eax, [...]`, `add esp, 8` with pops,
// `mov edx, [esi]`, `mov ecx, [esi+0x34]`); CopyJobs from 0x0060728B and
// 0x00608834 (`mov ecx, [esi+0x4C]`, EDX not read); DestroyJobs from
// 0x0060714C, 0x0060729C, 0x00607A96, 0x0060858A, 0x00608674, 0x00608845,
// 0x00608AA4 (each loads ECX and EDX before it reads them); Shutdown through
// the vtable only (Release 0x00608C38, Start 0x006087CF, DoMessage
// 0x00607F27, Tick 0x00607D8C, the network manager's ShutdownCrews
// 0x00610B9A: none reads ECX or EDX before loading it).
//
// Calls, each count from the asm's pushes for that call and the callee's own
// `ret N`:
//   - the allocation 0x0090CF54 and the free 0x0090CF63 (cdecl, one);
//     operator delete 0x005E5620 (cdecl, one); RZ::operator_new 0x009133DA
//     (cdecl, one: 0x2C);
//   - cGZMessage's constructor 0x009134D6 none (plain ret); a message's
//     0x14 SetType, 0x84 SetIGZUnknown, 0x2C SetData1, 0x40 SetData2, 0x54
//     SetData3 one each (one push before each call, no clean-up), 0x10
//     GetType, 0x28 GetVoid1, 0x38 GetData2, 0x4 and 0x8 none;
//   - the message server's (0x00B43CCC) 0x10 MessagePost two (the message,
//     0), 0x14 AddNotification and 0x18 RemoveNotification two (the target,
//     the message ID);
//   - the network manager (SC4::GetNetworkManager 0x00602680, cdecl, none)
//     0x40 RegisterConstructionCrew and 0x44 UnregisterConstructionCrew one
//     (the crew; ret 4 in src/core/netmgr_state.cpp's translations); the
//     frame service (SC4::GetService86AD10EC 0x00602600, cdecl, none) 0x18
//     two (the crew, 0xC0);
//   - the sound service's 0x4 and 0x8 none; a piece's 0x28 SetNetworkFlag
//     and 0x2C ClearNetworkFlag one, 0xC0 none, 0xEC two (x and z out), 0x4
//     and 0x8 none; an occupant's 0x18 none and 0x48 two (1, 1); a property
//     holder's 0x28 four (the property, 0, 1, 0); the service 0x00B43DC4's
//     0x58 none;
//   - SC4::GetPropertyUint32 0x005FD480 and SC4::SetPropertyUint32 0x005FE3C0
//     (cdecl, three each: `add esp, 0xC`);
//   - SC4Stopwatch::Construct 0x0088FEDF one (the mode 4; ret 4) and
//     SC4Stopwatch::Destruct 0x00A6D837 none (a bare ret);
//   - SC4PtrVector::Assign 0x0072BD50 one (ret 4), SC4PtrVector::Copy
//     0x004B03E0 one (ret 4), SC4List::Assign2 0x0064B660 one (ret 4),
//     SC4StdList::InsertRangeUint32 0x006F1570 four (where, first, last, a
//     dummy; ret 0x10), SC4PairList::Destroy 0x005650A0 none, SC4Vector12::Copy
//     0x006143E0 (cdecl, five pushed, three read: `add esp, 0x14`);
//   - this class: the destructor 0x00607130 none; CopyJobs (cdecl, five
//     pushed, three read) and DestroyJobs (cdecl, three); AssignJob one (ret
//     4); OnOccupantRemoved and Tick one (ret 4); slots 5 (Shutdown) and 6
//     (StaticInit) none; the second part's End 0x00606750 two (the job, 1;
//     ret 8), NextPiece 0x00607AD0 one (the job; ret 4; its answer in AL)
//     and the worker's Dismiss 0x00605800 one (the worker; ret 4).
#include "core/crew_sim.h"

#include "core/address_check.h"
#include "core/game.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace opensc4::crew_sim {

void FirstCall(const char* name, bool& logged)
{
    if (logged)
        return;
    logged = true;
    log::Write("crew: first %s", name);
}

// Hooks to leave out, for bisecting: OPENSC4_CREW_SKIP lists function
// addresses (hex, eight digits), as OPENSC4_NETLOT_SKIP does.
bool Skipped(std::uintptr_t address)
{
    char list[1024] = {};
    if (!GetEnvironmentVariableA("OPENSC4_CREW_SKIP", list, sizeof list))
        return false;
    char wanted[16];
    std::snprintf(wanted, sizeof wanted, "%08X", static_cast<unsigned>(address));
    return std::strstr(list, wanted) != nullptr;
}

bool InstallDetourList(const Detour* detours, std::size_t count, const char* what)
{
    if (!game::IsBuild641())
        return false;
    for (std::size_t i = 0; i < count; ++i)
        if (!addr_check::Find(detours[i].address)) {
            log::Write("crew %s: no address entry for 0x%08X", what, static_cast<unsigned>(detours[i].address));
            return false;
        }
    unsigned installed = 0;
    for (std::size_t i = 0; i < count; ++i) {
        if (Skipped(detours[i].address))
            continue;
        void* original = nullptr;
        if (!hook::Install(*addr_check::Find(detours[i].address), detours[i].ours, &original))
            return false;
        ++installed;
    }
    log::Write("crew %s: %u detours", what, installed);
    return true;
}

namespace {

void Free(void* memory)
{
    CallCdecl<void, void*>(addr::SC4_Free, memory);
}

ListNode* NewHead()
{
    auto* const head = CallCdecl<ListNode*, uint32_t>(addr::SC4_stl_allocate, uint32_t{kListNodeSize});
    head->next = head;
    head->prev = head;
    return head;
}

// The inline clear of a list the crew's code has in several places: every
// node freed (the pieces in them are not released), the head read again
// after each, then linked to itself.
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

Vector* VectorAt(char* object, std::size_t offset) { return reinterpret_cast<Vector*>(object + offset); }

// ====================================================================================
// simulators.crew.life
// ====================================================================================

// Slot 6, 0x00605620: the static byte 0x00B43E60 set when it is clear. AL 1
// always (`mov al, 1` either way); ECX and EDX untouched in the game.
uint8_t __fastcall StaticInit(char*, void*)
{
    static bool logged = false;
    FirstCall("StaticInit", logged);
    uint8_t& done = Global<uint8_t>(addr::cSC4NetworkConstructionCrew_StaticInitialized);
    if (!done)
        done = 1;
    return 1;
}

// Slot 7, 0x00605640: the byte cleared when it is set. AL 1 always. No
// caller found (no direct call; the vtable 0x00AA74AC is the only place
// that holds the address).
uint8_t __fastcall StaticShutdown(char*, void*)
{
    static bool logged = false;
    FirstCall("StaticShutdown", logged);
    uint8_t& done = Global<uint8_t>(addr::cSC4NetworkConstructionCrew_StaticInitialized);
    if (done)
        done = 0;
    return 1;
}

// 0x006070C0, no arguments, returns this. Only caller the network tool's
// 0x0063BE17 after operator new 0x54. Every field zero, the list's head made
// (the word at +0x2C is cleared before the allocation, as the game does).
char* __fastcall Construct(char* self, void*)
{
    static bool logged = false;
    FirstCall("constructor", logged);
    At<std::uintptr_t>(self, field::kVtable) = addr::cSC4NetworkConstructionCrew_vftable;
    At<uint32_t>(self, field::kPieceMilliseconds) = 0;
    At<uint32_t>(self, field::kScale) = 0;
    At<uint32_t>(self, field::kCount) = 0;
    At<uint32_t>(self, field::kRefCount) = 0;
    At<uint8_t>(self, field::kInitialized) = 0;
    At<uint8_t>(self, field::kInFrameService) = 0;
    At<void*>(self, field::kSound) = nullptr;
    At<uint32_t>(self, field::kUnused1C) = 0;
    At<uint32_t>(self, field::kWorkerValueA) = 0;
    At<uint32_t>(self, field::kWorkerValueB) = 0;
    At<uint32_t>(self, field::kValueC) = 0;
    At<ListNode*>(self, field::kPieces) = nullptr;
    At<ListNode*>(self, field::kPieces) = NewHead();
    for (std::size_t offset = field::kSegments; offset < kObjectSize; offset += 4)
        At<uint32_t>(self, offset) = 0; // the three vectors, +0x30 to +0x50
    return self;
}

// 0x00607130, no arguments, not virtual (only caller Release 0x00608C3A):
// the vtable stored, the jobs destroyed (DestroyJobs over the vector, its
// third argument the address of a local), the three vectors' memory freed
// (jobs, 8-byte records, segment records), then the piece list destroyed by
// SC4PairList::Destroy (nodes and head freed, nothing released).
void __fastcall Destroy(char* self, void*)
{
    static bool logged = false;
    FirstCall("destructor", logged);
    At<std::uintptr_t>(self, field::kVtable) = addr::cSC4NetworkConstructionCrew_vftable;
    uint8_t dummy = 0;
    Vector* const jobs = VectorAt(self, field::kJobs);
    CallCdecl<void, char*, char*, void*>(addr::cSC4NetworkConstructionCrew_DestroyJobs, jobs->begin, jobs->end,
                                         &dummy);
    if (jobs->begin)
        Free(jobs->begin);
    if (char* const records = VectorAt(self, field::kRecords)->begin)
        Free(records);
    if (char* const segments = VectorAt(self, field::kSegments)->begin)
        Free(segments);
    CallThis<void>(addr::SC4PairList_Destroy, self + field::kPieces);
}

// Slot 2, 0x00608C20: above 1 (signed) the count at +0x10 goes down by one
// and is the answer; else slot 5 (Shutdown), the destructor at its address,
// operator delete, and 0.
uint32_t __fastcall Release(char* self, void*)
{
    static bool logged = false;
    FirstCall("Release", logged);
    const int32_t count = At<int32_t>(self, field::kRefCount);
    if (count > 1) {
        At<int32_t>(self, field::kRefCount) = count - 1;
        return static_cast<uint32_t>(count - 1);
    }
    Virtual<uint8_t>(self, slot::kShutdown);
    CallThis<void>(addr::cSC4NetworkConstructionCrew_Destroy, self);
    CallCdecl<void, void*>(addr::SC4_operator_delete, self);
    return 0;
}

QuietObserver g_quiet = nullptr;
bool g_initOurs = false;

// Slot 4, 0x00607190, ret 0x18: Init(count, seconds, scale, a, b, c), the
// two floats as their 32 bits. Slot 6 when the static byte is clear; an
// initialised crew answers 1 at once. Otherwise: the network manager's slot
// 16 with the crew; +0xC the count, +0x4 the truncation of seconds times
// 1000.0f, +0x8 the float of scale times 1e-6f (each product of two floats
// is exact in the x87's 53 bits, so one rounding to float, as the game's
// fstp); +0x20, +0x24, +0x28 the last three. Then the sound service
// (0x006071F4): stored at +0x18 and referenced when there is one, unless
// the byte at 0x006071FC is the plugin's 0xEB (the game's 0x74 is a `je`
// over the store when there is none; the plugin's `jmp` always skips it) or
// game.quiet.network's switch is on. Then the piece list's nodes freed (no
// piece released), the segment records erased (SC4Vector12::Copy of the
// empty tail over the start: the new end is the begin), the 8-byte records
// erased inline (a copy loop of (end - end) / 8 = 0 turns, the end set to
// the begin), the jobs erased (CopyJobs of the empty tail, DestroyJobs of
// what is left after it, the end set), the two notifications added (when
// there is a message server), +0x14 set, AL 1.
uint8_t __fastcall Init(char* self, void*, uint32_t count, uint32_t secondsBits, uint32_t scaleBits, uint32_t a,
                        uint32_t b, uint32_t c)
{
    static bool logged = false;
    FirstCall("Init", logged);
    if (!Global<uint8_t>(addr::cSC4NetworkConstructionCrew_StaticInitialized))
        Virtual<uint8_t>(self, slot::kStaticInit);
    if (At<uint8_t>(self, field::kInitialized)) {
        if (g_quiet)
            g_quiet(false);
        return 1;
    }
    void* const manager = CallCdecl<void*>(addr::SC4_GetNetworkManager);
    Virtual<uint8_t>(manager, manager_slot::kRegisterConstructionCrew, static_cast<void*>(self));
    float seconds, scale;
    std::memcpy(&seconds, &secondsBits, sizeof seconds);
    std::memcpy(&scale, &scaleBits, sizeof scale);
    At<uint32_t>(self, field::kCount) = count;
    const double milliseconds =
        static_cast<double>(seconds) * static_cast<double>(Global<float>(addr::SC4_Float1000));
    At<int32_t>(self, field::kPieceMilliseconds) = static_cast<int32_t>(static_cast<int64_t>(milliseconds));
    At<float>(self, field::kScale) =
        static_cast<float>(static_cast<double>(scale) * static_cast<double>(Global<float>(addr::SC4_FloatMillionth)));
    At<uint32_t>(self, field::kWorkerValueA) = a;
    At<uint32_t>(self, field::kWorkerValueB) = b;
    At<uint32_t>(self, field::kValueC) = c;

    void* const sound = Global<void*>(addr::SC4_Sound43CB8);
    const uint8_t jump = Global<uint8_t>(addr::cSC4NetworkConstructionCrew_SoundJump);
    bool withheld = false;
    if (jump == kSoundJumpPlugin) {
        static bool loggedPlugin = false;
        if (!loggedPlugin)
            log::Write("crew: Init: the byte at 0x006071FC is 0xEB (sc4-disable-network-construction-sounds): the "
                       "sound service is not stored");
        loggedPlugin = true;
    } else if (sound) {
        if (g_quiet) {
            withheld = true;
            static bool loggedQuiet = false;
            if (!loggedQuiet)
                log::Write("crew: Init: game.quiet.network: the sound service is not stored (byte at 0x006071FC "
                           "0x%02X)",
                           jump);
            loggedQuiet = true;
        } else {
            At<void*>(self, field::kSound) = sound;
            AddRefOf(sound);
            static bool loggedStored = false;
            if (!loggedStored)
                log::Write("crew: Init: the sound service is stored (byte at 0x006071FC 0x%02X, no switch)", jump);
            loggedStored = true;
        }
    }

    FreeNodes(&At<ListNode*>(self, field::kPieces));
    uint8_t dummy = 0;
    Vector* const segments = VectorAt(self, field::kSegments);
    segments->end = CallCdecl<char*, char*, char*, char*, void*, uint32_t>(
        addr::SC4Vector12_Copy, segments->end, segments->end, segments->begin, &dummy, 0u);
    Vector* const records = VectorAt(self, field::kRecords);
    records->end = records->begin;
    Vector* const jobs = VectorAt(self, field::kJobs);
    char* const kept = CallCdecl<char*, char*, char*, char*, void*, uint32_t>(
        addr::cSC4NetworkConstructionCrew_CopyJobs, jobs->end, jobs->end, jobs->begin, &dummy, 0u);
    CallCdecl<void, char*, char*, void*>(addr::cSC4NetworkConstructionCrew_DestroyJobs, kept, jobs->end, &dummy);
    jobs->end = kept;
    if (void* const server = Global<void*>(addr::SC4_MessageServer)) {
        Virtual<uint8_t>(server, server_slot::kAddNotification, static_cast<void*>(self), kMsgOccupantRemoved);
        Virtual<uint8_t>(server, server_slot::kAddNotification, static_cast<void*>(self), kMsgPreCityShutdown);
    }
    At<uint8_t>(self, field::kInitialized) = 1;
    if (g_quiet)
        g_quiet(withheld);
    return 1;
}

// Slot 5, 0x00607920: nothing for a crew that is not initialised (AL 1).
// Else +0x14 cleared, the two notifications removed (when there is a
// message server), the frame service left when +0x15 is set and the service
// exists (+0x15 cleared just before the call), every job ended (the second
// part's End with 1, at its address; the count read again after each), the
// piece list's pieces finished and released (FinishPieces with 1, at its
// address) and its nodes freed, the segment records, the 8-byte records and
// the jobs erased (two inline loops of zero turns: each end set to its
// begin; the jobs by an AssignJob loop of zero turns and DestroyJobs over
// the whole vector), the sound service released and +0x18 cleared, and the
// network manager's slot 17 with the crew. That last call can release the
// crew's last reference (the manager's): nothing touches the crew after it.
// AL 1.
uint8_t __fastcall Shutdown(char* self, void*)
{
    static bool logged = false;
    FirstCall("Shutdown", logged);
    if (!At<uint8_t>(self, field::kInitialized))
        return 1;
    At<uint8_t>(self, field::kInitialized) = 0;
    if (void* const server = Global<void*>(addr::SC4_MessageServer)) {
        Virtual<uint8_t>(server, server_slot::kRemoveNotification, static_cast<void*>(self), kMsgOccupantRemoved);
        Virtual<uint8_t>(server, server_slot::kRemoveNotification, static_cast<void*>(self), kMsgPreCityShutdown);
    }
    if (At<uint8_t>(self, field::kInFrameService))
        if (void* const frames = CallCdecl<void*>(addr::SC4_GetService86AD10EC)) {
            At<uint8_t>(self, field::kInFrameService) = 0;
            Virtual<uint8_t>(frames, frame_slot::kRemove, static_cast<void*>(self), kFrameServiceCode);
        }
    Vector* const jobs = VectorAt(self, field::kJobs);
    // The count as the game takes it: `sar 7`, then compared unsigned.
    auto jobCount = [jobs] {
        return static_cast<uint32_t>(static_cast<int32_t>(jobs->end - jobs->begin) >> 7);
    };
    if (jobCount() != 0) {
        uint32_t index = 0, offset = 0;
        do {
            CallThis<void, char*, uint32_t>(addr::cSC4NetworkConstructionCrew_EndJob, self, jobs->begin + offset, 1u);
            ++index;
            offset += kJobSize;
        } while (index < jobCount());
    }
    ListNode** const pieces = &At<ListNode*>(self, field::kPieces);
    CallCdecl<void, ListNode**, uint32_t>(addr::cSC4NetworkConstructionCrew_FinishPieces, pieces, 1u);
    FreeNodes(pieces);
    Vector* const segments = VectorAt(self, field::kSegments);
    segments->end = segments->begin;
    Vector* const records = VectorAt(self, field::kRecords);
    records->end = records->begin;
    uint8_t dummy = 0;
    CallCdecl<void, char*, char*, void*>(addr::cSC4NetworkConstructionCrew_DestroyJobs, jobs->begin, jobs->end,
                                         &dummy);
    jobs->end = jobs->begin;
    if (void* const sound = At<void*>(self, field::kSound)) {
        ReleaseOf(sound);
        At<void*>(self, field::kSound) = nullptr;
    }
    void* const manager = CallCdecl<void*>(addr::SC4_GetNetworkManager);
    Virtual<uint8_t>(manager, manager_slot::kUnregisterConstructionCrew, static_cast<void*>(self));
    return 1;
}

// ====================================================================================
// simulators.crew.messages
// ====================================================================================

// Slot 3, 0x00607ED0, ret 4: by the message's type (its slot 4) 0x26D31EC2
// the crew's slot 5 (Shutdown); 0x86AD10EE Tick at its address with the
// message's data 2 (the frame's microseconds; sim_timers' hook is there);
// 0x99EF1143 OnOccupantRemoved at its address with the message's void 1.
// AL 1 always.
uint8_t __fastcall DoMessage(char* self, void*, void* message)
{
    static bool logged = false;
    FirstCall("DoMessage", logged);
    const uint32_t type = Virtual<uint32_t>(message, message_slot::kGetType);
    if (type == kMsgPreCityShutdown) {
        Virtual<uint8_t>(self, slot::kShutdown);
    } else if (type == kMsgFrame) {
        CallThis<void, uint32_t>(addr::cSC4NetworkConstructionCrew_Tick, self,
                                 Virtual<uint32_t>(message, message_slot::kGetData2));
    } else if (type == kMsgOccupantRemoved) {
        CallThis<void, void*>(addr::cSC4NetworkConstructionCrew_OnOccupantRemoved, self,
                              Virtual<void*>(message, message_slot::kGetVoid1));
    }
    return 1;
}

// 0x00607DA0, ret 4 (the occupant removed; only caller DoMessage). For
// every job (the count taken once; the vector's begin read again for each):
// the first piece of its list whose slot 0xC0 (its occupant) is the one
// removed. A worker on it (+0x58, +0x78) is dismissed (the second part's
// Dismiss at its address). Then the piece leaves the job's list (released,
// node unlinked and freed) when the job has not ended (+0x4 not -1, read
// before the call below) or, if it is the job's current piece (+0x8), when
// the job's NextPiece (at its address; AL) says there is a next one: when
// there is none, the job has ended and its list was finished and emptied by
// End. Then the first such piece of the crew's own list is released and its
// node unlinked and freed.
void __fastcall OnOccupantRemoved(char* self, void*, void* occupant)
{
    static bool logged = false;
    FirstCall("OnOccupantRemoved", logged);
    int32_t count = static_cast<int32_t>(VectorAt(self, field::kJobs)->end - VectorAt(self, field::kJobs)->begin) >> 7;
    if (count > 0) {
        uint32_t offset = 0;
        do {
            char* const job = VectorAt(self, field::kJobs)->begin + offset;
            ListNode* const head = At<ListNode*>(job, job::kPieces);
            for (ListNode* node = head->next; node != head; node = node->next) {
                void* const piece = node->value;
                if (occupant != Virtual<void*>(piece, piece_slot::kAsOccupant))
                    continue;
                if (At<void*>(job, job::kWorkerA + worker::kPiece) == piece)
                    CallThis<void, char*>(addr::cSC4NetworkConstructionCrew_DismissWorker, self, job + job::kWorkerA);
                if (At<void*>(job, job::kWorkerB + worker::kPiece) == piece)
                    CallThis<void, char*>(addr::cSC4NetworkConstructionCrew_DismissWorker, self, job + job::kWorkerB);
                uint8_t keep = At<int32_t>(job, job::kState) != -1;
                if (At<void*>(job, job::kPiece) == piece)
                    keep = CallThis<uint8_t, char*>(addr::cSC4NetworkConstructionCrew_NextJobPiece, self, job);
                if (keep) {
                    ReleaseOf(piece);
                    node->prev->next = node->next;
                    node->next->prev = node->prev;
                    Free(node);
                }
                break;
            }
            offset += kJobSize;
        } while (--count != 0);
    }
    ListNode* const head = At<ListNode*>(self, field::kPieces);
    for (ListNode* node = head->next; node != head; node = node->next) {
        void* const piece = node->value;
        if (occupant != Virtual<void*>(piece, piece_slot::kAsOccupant))
            continue;
        ReleaseOf(piece);
        node->prev->next = node->next;
        node->next->prev = node->prev;
        Free(node);
        return;
    }
}

// 0x006058C0, ret 0x10: PostMessage(type, object, data 2, data 3). A new
// framework message (RZ::operator_new 0x2C, cGZMessage's constructor; the
// game goes on with a null one, as it would crash, when the allocation
// fails), referenced, with the type, the object (SetIGZUnknown), the crew
// as data 1 and the two data words; posted (MessagePost, not high
// priority) and released. EAX is Release's answer. Callers: the job's Begin
// 0x006078F5 (0x89F10866) and NextPiece 0x00607BC6 (0xA9F10E45),
// 0x00607BF0 (0x89F10866), all only when the crew holds the sound service.
uint32_t __fastcall PostCrewMessage(char* self, void*, uint32_t type, void* object, uint32_t data2, uint32_t data3)
{
    static bool logged = false;
    FirstCall("PostMessage", logged);
    void* const memory = CallCdecl<void*, uint32_t>(addr::RZ_operator_new, 0x2Cu);
    void* const message = memory ? CallThis<void*>(addr::cGZMessage_cGZMessage, memory) : nullptr;
    if (message)
        AddRefOf(message);
    Virtual<void>(message, message_slot::kSetType, type);
    Virtual<void>(message, message_slot::kSetIGZUnknown, object);
    Virtual<void>(message, message_slot::kSetData1, static_cast<void*>(self));
    Virtual<void>(message, message_slot::kSetData2, data2);
    Virtual<void>(message, message_slot::kSetData3, data3);
    Virtual<uint8_t>(Global<void*>(addr::SC4_MessageServer), server_slot::kMessagePost, message, 0u);
    return ReleaseOf(message);
}

// ====================================================================================
// simulators.crew.pieces
// ====================================================================================

// What FinishPiece does to a piece, and FinishPieces inline to each: the
// 0x4000 network flag cleared when the service 0x00B43DC4's slot 22 answers
// 1 (asked for every piece), the occupant's slot 0x48 (SetVisibility) with
// 1 and 1, the usable flag set (SetNetworkFlag, which posts 0x89D3E9F3).
void Finish(void* piece)
{
    if (Virtual<int32_t>(Global<void*>(addr::SC4_Service43DC4), kService43DC4Slot22) == 1)
        Virtual<void>(piece, piece_slot::kClearNetworkFlag, kFlag4000);
    Virtual<void>(Virtual<void*>(piece, piece_slot::kAsOccupant), occupant_slot::kSetVisibility, 1u, 1u);
    Virtual<void>(piece, piece_slot::kSetNetworkFlag, kFlagUsable);
}

// 0x00605870, cdecl (the piece): nothing for a null piece, else Finish.
// Callers 0x00606195 and 0x0060677F (the second part's), each with a piece.
void __cdecl FinishPiece(void* piece)
{
    static bool logged = false;
    FirstCall("FinishPiece", logged);
    if (piece)
        Finish(piece);
}

// 0x00605B30, cdecl (the address of a list's head pointer, a byte: release):
// nothing for a null list; the head read once; for each node, its piece
// finished when it is not null, and released when the byte is set (the
// game releases a null piece too, and crashes: kept). Callers: Shutdown
// (1), Start 0x006087C0 (0) and 0x00608B63 (1), the job's End 0x0060681A
// (1), AddSegment 0x00606B73 and 0x0060709F (0), and the network tools
// 0x0062BC6C, 0x0062D3FA and 0x0063BD15 (1): this detour serves them all,
// with the game's convention.
void __cdecl FinishPieces(ListNode** list, uint8_t release)
{
    static bool logged = false;
    FirstCall("FinishPieces", logged);
    if (!list)
        return;
    ListNode* const head = *list;
    for (ListNode* node = head->next; node != head; node = node->next) {
        void* const piece = node->value;
        if (piece)
            Finish(piece);
        if (release)
            ReleaseOf(piece);
    }
}

// A piece's property holder: its occupant's (slot 0xC0, then the
// occupant's slot 0x18); null when it has none.
void* HolderOf(void* piece)
{
    return Virtual<void*>(Virtual<void*>(piece, piece_slot::kAsOccupant), occupant_slot::kAsPropertyHolder);
}

// 0x00605660, cdecl (the piece): the work property of the piece's occupant's
// property holder (none: 0) raised by one, added first (the holder's slot
// 0x28 with the property, 0, 1, 0) when the piece has none. The answer is
// the value read before (0 when it was added). Callers: Start (0x00608122,
// 0x006081AD, 0x006083BD, 0x0060842A).
uint32_t __cdecl IncrementWork(void* piece)
{
    static bool logged = false;
    FirstCall("IncrementWork", logged);
    uint32_t value = 0;
    void* const holder = HolderOf(piece);
    if (!holder)
        return value;
    if (!CallCdecl<uint8_t, void*, uint32_t, uint32_t*>(addr::SC4_GetPropertyUint32, holder, kWorkProperty, &value))
        Virtual<uint8_t>(holder, kHolderAddProperty, kWorkProperty, 0u, 1u, 0u);
    CallCdecl<void, void*, uint32_t, uint32_t>(addr::SC4_SetPropertyUint32, holder, kWorkProperty, value + 1);
    return value;
}

// 0x006056D0, cdecl (the piece): the same property lowered by one, never
// below 0 (at 0 nothing is written). The answer is the new value (0 at 0 or
// without a holder). Only caller Tick 0x00607CE6.
uint32_t __cdecl DecrementWork(void* piece)
{
    static bool logged = false;
    FirstCall("DecrementWork", logged);
    uint32_t value = 0;
    void* const holder = HolderOf(piece);
    if (!holder)
        return value;
    if (!CallCdecl<uint8_t, void*, uint32_t, uint32_t*>(addr::SC4_GetPropertyUint32, holder, kWorkProperty, &value))
        Virtual<uint8_t>(holder, kHolderAddProperty, kWorkProperty, 0u, 1u, 0u);
    if (value == 0)
        return 0;
    --value;
    CallCdecl<void, void*, uint32_t, uint32_t>(addr::SC4_SetPropertyUint32, holder, kWorkProperty, value);
    return value;
}

// 0x00605A40, ret 4 (the job): the job made ready from the crew: state 0,
// milliseconds per piece from the crew's +0x4, its list's nodes freed (no
// piece released), current piece and effect cleared, worker A: byte 0,
// vehicle and piece cleared, value the crew's +0x20, arrived 1; worker B:
// byte 1 (not 0 as A's), vehicle and piece cleared, value the crew's +0x24,
// arrived 1. AL 1 (`mov al, 1`; the rest of the game's EAX not reproduced).
// Callers: PlanJobs 0x00607F6F and 0x00607F7B, Start 0x006088A6.
uint8_t __fastcall ResetJob(char* self, void*, char* job)
{
    static bool logged = false;
    FirstCall("ResetJob", logged);
    At<int32_t>(job, job::kState) = 0;
    At<uint32_t>(job, job::kMilliseconds) = At<uint32_t>(self, field::kPieceMilliseconds);
    FreeNodes(&At<ListNode*>(job, job::kPieces));
    At<void*>(job, job::kPiece) = nullptr;
    At<void*>(job, job::kEffect) = nullptr;
    At<uint8_t>(job, job::kWorkerA + worker::kFlag) = 0;
    At<void*>(job, job::kWorkerA + worker::kVehicle) = nullptr;
    At<void*>(job, job::kWorkerA + worker::kPiece) = nullptr;
    At<uint32_t>(job, job::kWorkerA + worker::kValue) = At<uint32_t>(self, field::kWorkerValueA);
    At<uint8_t>(job, job::kWorkerA + worker::kArrived) = 1;
    At<uint8_t>(job, job::kWorkerB + worker::kFlag) = 1;
    At<void*>(job, job::kWorkerB + worker::kVehicle) = nullptr;
    At<void*>(job, job::kWorkerB + worker::kPiece) = nullptr;
    At<uint32_t>(job, job::kWorkerB + worker::kValue) = At<uint32_t>(self, field::kWorkerValueB);
    At<uint8_t>(job, job::kWorkerB + worker::kArrived) = 1;
    return 1;
}

// 0x00605AC0, ret 0x1C: SegmentStep(x*, z*, dx*, dz*, stepX*, stepZ*,
// index). True (AL 1) when (*x, *z) is the 8-byte record `index` of the
// vector at +0x3C (the end of the segment). Otherwise one step: when
// *stepX, *x = *dx + *x; else when *stepZ, *z += *dz; then both step bytes
// flipped (each becomes whether it was 0); AL 0. Callers: PlanJobs
// 0x0060817C and 0x00608201.
uint8_t __fastcall SegmentStep(char* self, void*, uint32_t* x, uint32_t* z, const uint32_t* dx, const uint32_t* dz,
                               uint8_t* stepX, uint8_t* stepZ, int32_t index)
{
    static bool logged = false;
    FirstCall("SegmentStep", logged);
    const uint32_t* const record = reinterpret_cast<const uint32_t*>(VectorAt(self, field::kRecords)->begin) +
                                   static_cast<std::ptrdiff_t>(index) * 2;
    const uint32_t atX = *x;
    if (atX == record[0] && *z == record[1])
        return 1;
    if (*stepX)
        *x = *dx + atX;
    else if (*stepZ)
        *z += *dz;
    *stepX = *stepX == 0;
    *stepZ = *stepZ == 0;
    return 0;
}

// The four-step absolute value of the game's `cdq; xor; sub`.
uint32_t Abs(uint32_t value)
{
    const uint32_t sign = static_cast<uint32_t>(static_cast<int32_t>(value) >> 31);
    return (value ^ sign) - sign;
}

// 0x00605BB0, ret 0x10: NextPiece(list*, current*, out*, adjacent*), `this`
// not read. False (AL 0) for an empty list or when *current is not in it.
// Else *out is the piece after it (null and *adjacent 0 when it was the
// last); otherwise *adjacent says whether the two pieces' cells (each's slot
// 0xEC) are one apart (|dx| + |dz| == 1, in 32-bit arithmetic). The piece
// in *out, if any, is referenced; AL is whether there is one. Callers: the
// second part's 0x006061DB, 0x0060628C, 0x006063FB, 0x00607B14.
uint8_t __fastcall NextPiece(char*, void*, ListNode** list, void** current, void** out, uint8_t* adjacent)
{
    static bool logged = false;
    FirstCall("NextPiece", logged);
    ListNode* const head = *list;
    ListNode* node = head->next;
    if (node == head)
        return 0;
    for (;;) {
        const bool found = node->value == *current;
        node = node->next;
        if (found)
            break;
        if (node == head)
            return 0;
    }
    if (node == head) {
        *out = nullptr;
        *adjacent = 0;
    } else {
        *out = node->value;
        uint32_t x1 = 0, z1 = 0, x2 = 0, z2 = 0;
        Virtual<void>(*current, piece_slot::kGetOccupiedCell, &x1, &z1);
        Virtual<void>(*out, piece_slot::kGetOccupiedCell, &x2, &z2);
        *adjacent = Abs(x2 - x1) + Abs(z2 - z1) - 1 == 0;
    }
    if (*out)
        AddRefOf(*out);
    return *out != nullptr;
}

// 0x00605C60, ret 0x10: FindPieceAtCell(out*, list*, x, z), `this` not
// read: *out is the first node whose piece's cell (slot 0xEC) is (x, z), or
// the head when none is. EAX is `out` (the callers read [eax]). Callers:
// AddSegment 0x00606FDB and 0x0060700A, PlanJobs 0x006080E7, 0x006083AD,
// 0x0060841A.
ListNode** __fastcall FindPieceAtCell(char*, void*, ListNode** out, ListNode** list, int32_t x, int32_t z)
{
    static bool logged = false;
    FirstCall("FindPieceAtCell", logged);
    ListNode* const head = *list;
    for (ListNode* node = head->next; node != head; node = node->next) {
        int32_t atX = 0, atZ = 0;
        Virtual<void>(node->value, piece_slot::kGetOccupiedCell, &atX, &atZ);
        if (x == atX && z == atZ) {
            *out = node;
            return out;
        }
    }
    *out = *list;
    return out;
}

// 0x00605CC0, no arguments, returns the job: the pedestrian vector cleared,
// the list's head made (the word cleared first), the stopwatch constructed
// in mode 4 (SC4Stopwatch::Construct, at its address), and the word at
// 0x00B43E64 (read anew each time) into each of the two workers' three
// floats. The other fields are left as they are. Callers: PlanJobs
// 0x00607F43 and 0x00607F4C, Start 0x0060889A (jobs on their stacks).
char* __fastcall ConstructJob(char* job, void*)
{
    static bool logged = false;
    FirstCall("ConstructJob", logged);
    At<char*>(job, job::kPedestrians) = nullptr;
    At<char*>(job, job::kPedestrians + 4) = nullptr;
    At<char*>(job, job::kPedestrians + 8) = nullptr;
    At<ListNode*>(job, job::kPieces) = nullptr;
    At<ListNode*>(job, job::kPieces) = NewHead();
    CallThis<void, uint32_t>(addr::SC4Stopwatch_Construct, job + job::kStopwatch, 4u);
    for (const std::size_t w : {job::kWorkerA, job::kWorkerB})
        for (std::size_t i = 0; i < 3; ++i)
            At<uint32_t>(job, w + worker::kPoint + i * 4) =
                Global<uint32_t>(addr::cSC4NetworkConstructionCrew_JobPointDefault);
    return job;
}

// The part AssignJob and CopyJob copy alike after the containers: the
// stopwatch's 0x18 bytes, the byte at +0x38, and the two workers field by
// field (the padding bytes between are not copied).
void CopyJobTail(char* to, const char* from)
{
    char* const src = const_cast<char*>(from);
    for (std::size_t i = 0; i < job::kStopwatchSize; i += 4)
        At<uint32_t>(to, job::kStopwatch + i) = At<uint32_t>(src, job::kStopwatch + i);
    At<uint8_t>(to, job::kFlag38) = At<uint8_t>(src, job::kFlag38);
    for (const std::size_t w : {job::kWorkerA, job::kWorkerB}) {
        At<uint8_t>(to, w + worker::kFlag) = At<uint8_t>(src, w + worker::kFlag);
        for (std::size_t i = worker::kValue; i < worker::kArrived; i += 4) // the value, the vehicle, the point
            At<uint32_t>(to, w + i) = At<uint32_t>(src, w + i);
        At<uint8_t>(to, w + worker::kArrived) = At<uint8_t>(src, w + worker::kArrived);
        At<uint8_t>(to, w + worker::kFlag19) = At<uint8_t>(src, w + worker::kFlag19);
        At<uint32_t>(to, w + worker::kPiece) = At<uint32_t>(src, w + worker::kPiece);
    }
}

// 0x006068A0, ret 4 (the source), returns this: operator= of a job. The
// four words at +0, the pedestrian vector (SC4PtrVector::Assign), the piece
// list (SC4List::Assign2), then CopyJobTail. Callers: CopyJobs 0x00606AB3,
// Shutdown's loop of zero turns 0x00607A73.
char* __fastcall AssignJob(char* self, void*, char* from)
{
    static bool logged = false;
    FirstCall("AssignJob", logged);
    for (std::size_t i = 0; i < job::kPedestrians; i += 4)
        At<uint32_t>(self, i) = At<uint32_t>(from, i);
    CallThis<void*, char*>(addr::SC4PtrVector_Assign, self + job::kPedestrians, from + job::kPedestrians);
    CallThis<void*, char*>(addr::SC4List_Assign2, self + job::kPieces, from + job::kPieces);
    CopyJobTail(self, from);
    return self;
}

// 0x00606980, ret 4 (the source), returns this: the copy constructor. The
// four words, the pedestrian vector copied (SC4PtrVector::Copy), the list
// head made (the word cleared first) and the source's nodes inserted before
// it (SC4StdList::InsertRangeUint32 with where = the new head's next, the
// source's first and its head, and the address of the argument as the
// dummy), then CopyJobTail. Callers: Start (nine sites, 0x006084E0 to
// 0x00608A87), growing the job vector.
char* __fastcall CopyJob(char* self, void*, char* from)
{
    static bool logged = false;
    FirstCall("CopyJob", logged);
    for (std::size_t i = 0; i < job::kPedestrians; i += 4)
        At<uint32_t>(self, i) = At<uint32_t>(from, i);
    CallThis<void*, char*>(addr::SC4PtrVector_Copy, self + job::kPedestrians, from + job::kPedestrians);
    At<ListNode*>(self, job::kPieces) = nullptr;
    ListNode* const head = NewHead();
    At<ListNode*>(self, job::kPieces) = head;
    ListNode* const fromHead = At<ListNode*>(from, job::kPieces);
    CallThis<void, ListNode*, ListNode*, ListNode*, void*>(addr::SC4StdList_InsertRangeUint32, self + job::kPieces,
                                                           head->next, fromHead->next, fromHead, &from);
    CopyJobTail(self, from);
    return self;
}

// 0x00606A90, cdecl (first, last, destination; callers push two more words
// that are not read): AssignJob from each source job to each destination
// job, (last - first) >> 7 of them (signed; none when not above 0). The
// answer is the destination's end. Callers: Init 0x00607286, Start
// 0x0060882F (both erase the vector: an empty source range).
char* __cdecl CopyJobs(char* first, char* last, char* destination)
{
    static bool logged = false;
    FirstCall("CopyJobs", logged);
    int32_t count = static_cast<int32_t>(last - first) >> 7;
    if (count <= 0)
        return destination;
    do {
        CallThis<char*, char*>(addr::cSC4NetworkConstructionCrew_AssignJob, destination, first);
        first += kJobSize;
        destination += kJobSize;
    } while (--count != 0);
    return destination;
}

// 0x00606AE0, cdecl (first, last, a third word that is not read): for each
// job, the stopwatch's destructor (SC4Stopwatch::Destruct, a bare ret, at
// its address), the list's nodes freed and its head linked to itself, then
// the head freed when not null, and the pedestrian vector's memory freed
// when not null. Nothing is released (a job's references are given back by
// End before). Callers: the destructor, Init, Shutdown, Start (four sites).
void __cdecl DestroyJobs(char* first, char* last, void*)
{
    static bool logged = false;
    FirstCall("DestroyJobs", logged);
    for (char* job = first; job != last; job += kJobSize) {
        CallThis<void>(addr::SC4Stopwatch_Destruct, job + job::kStopwatch);
        FreeNodes(&At<ListNode*>(job, job::kPieces));
        if (ListNode* const head = At<ListNode*>(job, job::kPieces))
            Free(head);
        if (char* const pedestrians = At<char*>(job, job::kPedestrians))
            Free(pedestrians);
    }
}

// ====================================================================================
// Registration
// ====================================================================================

// Init has its own install (InstallLife): it is the entry game.quiet.network
// wraps when ours is not there.
const Detour kLife[] = {
    OPENSC4_CREW_DETOUR(addr::cSC4NetworkConstructionCrew_cSC4NetworkConstructionCrew, Construct),
    OPENSC4_CREW_DETOUR(addr::cSC4NetworkConstructionCrew_Destroy, Destroy),
    OPENSC4_CREW_DETOUR(addr::cSC4NetworkConstructionCrew_Release, Release),
    OPENSC4_CREW_DETOUR(addr::cSC4NetworkConstructionCrew_Shutdown, Shutdown),
    OPENSC4_CREW_DETOUR(addr::cSC4NetworkConstructionCrew_StaticInit, StaticInit),
    OPENSC4_CREW_DETOUR(addr::cSC4NetworkConstructionCrew_StaticShutdown, StaticShutdown),
};

const Detour kMessages[] = {
    OPENSC4_CREW_DETOUR(addr::cSC4NetworkConstructionCrew_DoMessage, DoMessage),
    OPENSC4_CREW_DETOUR(addr::cSC4NetworkConstructionCrew_OnOccupantRemoved, OnOccupantRemoved),
    OPENSC4_CREW_DETOUR(addr::cSC4NetworkConstructionCrew_PostMessage, PostCrewMessage),
};

const Detour kPieces[] = {
    OPENSC4_CREW_DETOUR(addr::cSC4NetworkConstructionCrew_FinishPiece, FinishPiece),
    OPENSC4_CREW_DETOUR(addr::cSC4NetworkConstructionCrew_FinishPieces, FinishPieces),
    OPENSC4_CREW_DETOUR(addr::cSC4NetworkConstructionCrew_IncrementWork, IncrementWork),
    OPENSC4_CREW_DETOUR(addr::cSC4NetworkConstructionCrew_DecrementWork, DecrementWork),
    OPENSC4_CREW_DETOUR(addr::cSC4NetworkConstructionCrew_ResetJob, ResetJob),
    OPENSC4_CREW_DETOUR(addr::cSC4NetworkConstructionCrew_SegmentStep, SegmentStep),
    OPENSC4_CREW_DETOUR(addr::cSC4NetworkConstructionCrew_NextPiece, NextPiece),
    OPENSC4_CREW_DETOUR(addr::cSC4NetworkConstructionCrew_FindPieceAtCell, FindPieceAtCell),
    OPENSC4_CREW_DETOUR(addr::cSC4NetworkConstructionCrew_ConstructJob, ConstructJob),
    OPENSC4_CREW_DETOUR(addr::cSC4NetworkConstructionCrew_AssignJob, AssignJob),
    OPENSC4_CREW_DETOUR(addr::cSC4NetworkConstructionCrew_CopyJob, CopyJob),
    OPENSC4_CREW_DETOUR(addr::cSC4NetworkConstructionCrew_CopyJobs, CopyJobs),
    OPENSC4_CREW_DETOUR(addr::cSC4NetworkConstructionCrew_DestroyJobs, DestroyJobs),
};

// simulators.crew.life installs at PreAppInit, before game.quiet.network
// (PostAppInit) asks InitIsOurs. Init goes in unless the skip list names it
// or a detour of ours is already at its entry (another order of the phases
// than today's: ours then stays out and the wrapper keeps the game's Init);
// either way the group's other detours go in.
bool InstallLife(registry::Mode)
{
    if (!InstallDetours(kLife, "life"))
        return false;
    const addr::Entry* const init = addr_check::Find(addr::cSC4NetworkConstructionCrew_Init);
    if (!init)
        return false;
    if (Skipped(init->address)) {
        log::Write("crew life: Init on the skip list: the game's");
        return true;
    }
    if (hook::IsInstalledAt(init->address)) {
        log::Write("crew life: Init is already hooked; ours not installed there");
        return true;
    }
    void* original = nullptr;
    g_initOurs = hook::Install(*init, reinterpret_cast<void*>(&Init), &original);
    log::Write("crew life: Init %s", g_initOurs ? "is ours" : "NOT installed (hook failed)");
    return g_initOurs;
}

bool InstallMessages(registry::Mode) { return InstallDetours(kMessages, "messages"); }
bool InstallPieces(registry::Mode) { return InstallDetours(kPieces, "pieces"); }

const bool g_registered[] = {
    registry::Register({"simulators.crew.life", "simulators/crew", registry::Kind::Detour,
                        registry::Phase::PreAppInit, // before game.quiet.network asks InitIsOurs (crew_sim.h)
                        addr::cSC4NetworkConstructionCrew_Init,
                        "Our network construction crew life: constructor, destructor, Release, Init (with "
                        "game.quiet.network's switch and the sounds plugin's byte), Shutdown and the static pair (7 "
                        "detours; src/core/crew_life.cpp)",
                        false, InstallLife}),
    registry::Register({"simulators.crew.messages", "simulators/crew", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4NetworkConstructionCrew_DoMessage,
                        "Our network construction crew messages: DoMessage, OnOccupantRemoved and PostMessage (3 "
                        "detours; src/core/crew_life.cpp)",
                        false, InstallMessages}),
    registry::Register({"simulators.crew.pieces", "simulators/crew", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4NetworkConstructionCrew_FinishPieces,
                        "Our network construction crew pieces: FinishPiece, FinishPieces (the network tools' too), "
                        "the work property counters, and the job record's and the piece list's helpers (13 "
                        "detours; src/core/crew_life.cpp)",
                        false, InstallPieces}),
};

} // namespace

bool InitIsOurs() { return g_initOurs; }

void SetQuietObserver(QuietObserver observer) { g_quiet = observer; }

} // namespace opensc4::crew_sim
