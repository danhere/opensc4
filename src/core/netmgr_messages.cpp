// Our cSC4NetworkManager, third part, first half (build 641; the class map,
// the vtables in their true order, the fields and the messages are in
// src/core/netmgr_sim.h): messages and removals. DoMessage (the slot of the
// message target at +4) with the occupant callback it hands out and
// RefreshAllPieces; DestroyNetworkOccupants (slot 6), QueueRemovals (slot 7)
// and RemoveOrReplaceOccupant (slot 8); and the template code only this
// class calls: the sort of a range of pointers and the vector of point
// vectors of the cell-change flush. Each is a detour at the game's address,
// and every call from one of them to another goes through the game's
// address or the vtable, so a group that is off, or a function on the skip
// list, is the game's.
//
// Every function was written from the disassembly. PieceOffTerrain
// 0x006105F0 takes its arguments in registers (ECX the cell's x, EAX its z,
// EDI the piece, ESI the terrain) and has one caller, DoMessage's
// 0x00613BEA: it is written here as a function DoMessage calls and is not
// hooked.
//
// The detours are plain functions, so they clobber ECX and EDX. The game's
// functions that keep both on some path: QueueRemovals 0x00613450 when the
// byte at +0x74 has the value already or the new value is not 0 (the test
// for an empty queue loads ECX); DestroyNetworkOccupants 0x00612110 while
// queueing with an empty list; RemoveOrReplaceOccupant 0x006110B0 for
// a null occupant; RefreshAllPieces 0x00612690 for two equal values; the
// sort 0x006132E0 for an empty range and its loop 0x00612E00 for a range of
// 16 or fewer; the insert 0x00613770 for a count of 0; and the loops
// 0x00611BD0 and 0x00611C00 for an empty range and 0x00611C30 for a count
// of 0. Their callers: the three slots are reached through the vtable only
// (no direct call in the exe); RefreshAllPieces from 0x00613FF2 (then
// `mov al, 1` and the pops); the sort from 0x00613483 (then `mov ecx,
// ds:0xb207e4`); the loop from 0x00613314 (then `mov edx, [esp+0x20]` and
// three pushes) and from itself at 0x00612E79 (then EBX, ESI, and
// `mov ecx, [edi]` at the loop's top); the insert from 0x00613907 (then
// three pops); 0x00611BD0 from 0x0061371A, 0x006137ED and 0x0061384C,
// 0x00611C00 from 0x00613816 and 0x00613866, 0x00611C30 from 0x0061382D
// (each loads the registers it reads next again). None reads a register it
// did not load again after the call.
//
// Left as the game's and called at their addresses: ProcessRetrofitList
// 0x00612820 (the second half of this part), ShutdownCrews 0x00610B80 and
// ReleaseTools 0x00610880 (the second part's, hooked there); the tools'
// code (0x006588D0, 0x00633220 and the tools' slots); the partition
// 0x00611840 (also called from 0x0047EAA8 and 0x0048ACA8) and the point
// vector's assignment 0x00611B00 (eighteen callers in other classes); the
// two sorts of small ranges 0x0048A460 and 0x0048A4B0; the containers'
// shared helpers (0x004F20C0, 0x00414270, 0x00413FE0, 0x00467B50,
// 0x004F5A00, 0x00427640, 0x005650A0, 0x004467A0, 0x007CED60, 0x004638B0,
// 0x007EBD30, 0x0051CA60, 0x008BC520); the network filter's constructor
// 0x0061F510.
//
// Calls, each count from the asm's pushes for that call and the callee's own
// `ret N`:
//   - a message's slots 0x10 (type), 0x24 and 0x28 (data 1), 0x38 and 0x3C
//     (data 2) and 0x60 (data 4): none (no push before any; the vtable
//     0x00ACEC00's functions end in a plain ret);
//   - QueryInterface (slot 0) two (the ID, the out word); AddRef and Release
//     none;
//   - a piece's slots: 0x14 IsManagedGlobally none, 0x20 HasNetworkFlag one
//     (ret 4), 0x30 GetNetworkFlag none, 0x58 IsOfType one (a jump to slot
//     8), 0xB8 MarkLightingUpdateNeeded none, 0xE0 GetAltitude two floats
//     (ret 8), 0xEC GetOccupiedCell two (ret 8), 0xF4 SetLit one (ret 4),
//     0xF8 CanBeLit none;
//   - an occupant's slots: 0x3C PostOccupantMessage two (ret 8), 0x80
//     IsFlagSet one (ret 4), 0x84 GetFlags none;
//   - the occupant manager's (0x00B43D0C): 0x54 RemoveOccupant three (ret
//     0xC), 0x94 GetOccupantsByBBox by type five (ret 0x14), 0xA0
//     GetOccupantsByStandardCityCells five (ret 0x14), 0xB4 IterateOccupants
//     by type five (ret 0x14), 0xC0 ReleaseOccupantList one (ret 4);
//   - the city's slots 0x174 and 0x178 none; the terrain's slot 0x34 two
//     floats (two pushes each time); the clock's slot 0x70 none;
//   - a tool's slots 0xC Init and 0x10 Shutdown none, 0xC8 one (ret 4),
//     0x120 three (ret 0xC); 0x00633220 one (ret 4); 0x006588D0 five words
//     (a point vector by value, which is three, the list and the type; ret
//     0x14);
//   - this class through the vtable: slot 5 GetNetworkTool two, slot 6
//     DestroyNetworkOccupants one; at their addresses: ProcessRetrofitList
//     and ShutdownCrews none, ReleaseTools (cdecl, three), RefreshAllPieces
//     two (ret 8), the sort (cdecl, two), its loop (cdecl, five), the
//     resize 0x00613920 one (ret 4) and 0x006138B0 two (ret 8), the insert
//     three (ret 0xC), the growing insert five (ret 0x14), the erase two
//     (ret 8), 0x00611BD0 (cdecl, four), 0x00611C00 and 0x00611C30 (cdecl,
//     three), 0x00611C60 and 0x00611CB0 (cdecl, five);
//   - the partition 0x00611840 (cdecl, four), 0x0048A460 (cdecl, three),
//     0x0048A4B0 (cdecl, five), the assignment 0x00611B00 one (ret 4);
//   - the reference holder 0x004F20C0 one (ret 4); the list's constructor
//     0x00414270 and destructor 0x00413FE0 none, push_back 0x00467B50 one
//     (ret 4); the plain list's constructor 0x004F5A00 one (ret 4),
//     push_back 0x00427640 one (ret 4), destructor 0x005650A0 none; the
//     point vector's push_back 0x004467A0 one (ret 4) and copy constructor
//     0x007CED60 one (ret 4); the vector of vectors' destructor 0x004638B0
//     none; the pointer vector's push_back 0x007EBD30 one (ret 4) and
//     growing insert 0x0051CA60 five (ret 0x14); the find 0x008BC520 (cdecl,
//     three); the filter's constructor 0x0061F510 none; operator new
//     0x005E55E0, the allocation 0x0090CF54 and the free 0x0090CF63 (cdecl,
//     one); SC4::GetNetworkManager 0x00602680 (cdecl, none); the node
//     allocator's slot 0xC one.
#include "core/netmgr_sim.h"

#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>

namespace opensc4::netmgr_sim {
namespace {

// The occupant type of every network piece, the mask word of a network
// filter, and everything for the occupant manager's limit.
constexpr uint32_t kTypeNetworkOccupant = 0xC772BF98;
constexpr std::size_t kFilterMask = 0xC;
constexpr uint32_t kNoLimit = 0xFFFFFFFF;

void FreeMemory(void* memory)
{
    CallCdecl<void, void*>(addr::SC4_Free, memory);
}

// The occupant manager (0x00B43D0C; the address entry is called
// SC4::NetworkService by an old mistake).
void* Occupants()
{
    return Global<void*>(addr::SC4_NetworkService);
}

// The piece an occupant of a list of network occupants is: the occupant
// part is the piece + 4, and the game steps back with a test for null.
char* PieceOf(void* occupant)
{
    return occupant ? static_cast<char*>(occupant) - 4 : nullptr;
}

// A new network filter without a mask (0x10 bytes, constructor 0x0061F510)
// in a reference holder (0x004F20C0, which references what is not null),
// and the mask stored at +0xC as DoMessage does it: without a test for
// null.
void* MakeFilter(uint32_t mask)
{
    void* const raw = CallCdecl<void*, std::size_t>(addr::SC4_operator_new, kNetworkFilterSize);
    void* const made = raw ? CallThis<void*>(addr::cSC4NetworkManager_NetworkFilterConstructDefault, raw) : nullptr;
    void* filter = nullptr;
    CallThis<void*, void*>(addr::SC4RefHolder_SC4RefHolder, &filter, made);
    At<uint32_t>(filter, kFilterMask) = mask;
    return filter;
}

// The number of 0xC-byte elements between two addresses: the game's signed
// division of the byte difference by 12 (a multiplication by 0x2AAAAAAB, a
// shift and the sign bit: towards zero).
int32_t ListCount(const char* first, const char* last)
{
    return static_cast<int32_t>(last - first) / static_cast<int32_t>(kPointVectorSize);
}

// ====================================================================================
// simulators.netmgr.removals: the sort of a range of pointers
// ====================================================================================

// 0x00612E00 (cdecl: first, last, a null that is not read, the depth, the
// comparison; QueueRemovals' sort and this function are its only callers):
// the loop of an introsort over unsigned 32-bit words, ascending. While the
// range is longer than 16 words (the byte difference without its low two
// bits, compared as a signed number with 0x40): with no depth left the
// range is sorted by 0x0048A4B0 (first, last, last, 0, the comparison) and
// the loop ends; else the depth goes down by one, the median of the first
// word, the middle word (the count halved towards zero) and the last word is
// taken, the range is partitioned around its value (0x00611840), the upper
// part is sorted by a call at this function's address and the loop goes on
// with the lower part. The comparison is an empty object: a word nobody
// reads, handed on as it came.
void __cdecl IntroSortLoop(uint32_t* first, uint32_t* last, uint32_t, uint32_t depth, uint32_t compare)
{
    static bool logged = false;
    FirstCall("IntroSortLoop", logged);
    const auto bytes = [first](const uint32_t* end) {
        const std::ptrdiff_t difference = reinterpret_cast<const char*>(end) - reinterpret_cast<const char*>(first);
        return static_cast<int32_t>(static_cast<uint32_t>(difference) & 0xFFFFFFFCu);
    };
    while (bytes(last) > 0x40) {
        if (depth == 0) {
            CallCdecl<void, uint32_t*, uint32_t*, uint32_t*, uint32_t, uint32_t>(addr::SC4_PartialSortUint32, first,
                                                                                 last, last, 0u, compare);
            return;
        }
        --depth;
        uint32_t* const middle = first + static_cast<int32_t>(last - first) / 2;
        uint32_t* const back = last - 1;
        const uint32_t a = *first, b = *middle, c = *back;
        uint32_t* median;
        if (a < b)
            median = b < c ? middle : (a < c ? back : first);
        else
            median = a < c ? first : (b < c ? back : middle);
        uint32_t* const cut = CallCdecl<uint32_t*, uint32_t*, uint32_t*, uint32_t, uint32_t>(
            addr::cSC4NetworkManager_PartitionPointers, first, last, *median, compare);
        CallCdecl<void, uint32_t*, uint32_t*, uint32_t, uint32_t, uint32_t>(addr::cSC4NetworkManager_IntroSortLoop,
                                                                            cut, last, 0u, depth, compare);
        last = cut;
    }
}

// 0x006132E0 (cdecl: first, last; QueueRemovals is its only caller): the
// range sorted as unsigned words, ascending (the occupants' addresses).
// Nothing for an empty range. The depth is twice the number of times the
// count can be halved before it is 1; then the loop 0x00612E00 and the last
// pass 0x0048A460 (first, last, the comparison), both at their addresses.
// For the comparison the game pushes the word in its own first argument's
// place, the range's start: the same here.
void __cdecl SortPointers(uint32_t* first, uint32_t* last)
{
    static bool logged = false;
    FirstCall("SortPointers", logged);
    if (first == last)
        return;
    uint32_t halvings = 0;
    for (int32_t count = static_cast<int32_t>(last - first); count != 1; count >>= 1)
        ++halvings;
    const uint32_t compare = static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(first));
    CallCdecl<void, uint32_t*, uint32_t*, uint32_t, uint32_t, uint32_t>(addr::cSC4NetworkManager_IntroSortLoop, first,
                                                                        last, 0u, halvings + halvings, compare);
    CallCdecl<void, uint32_t*, uint32_t*, uint32_t>(addr::SC4_FinalInsertionSortUint32, first, last, compare);
}

// ====================================================================================
// simulators.netmgr.removals: DestroyNetworkOccupants, QueueRemovals,
// RemoveOrReplaceOccupant
// ====================================================================================

// A pointer appended to a vector as the game has it inline: in place when
// there is room (the store is skipped for a null end, the game's placement
// test; the end is read again for the step), else through the growing
// insert 0x0051CA60 (at the end, the address of the value, a tag the insert
// does not read, one copy, at the end).
void Append(Vector* vector, void** value, void* tag)
{
    char* const end = vector->end;
    if (end != vector->capacity) {
        if (end)
            At<void*>(end, 0) = *value;
        vector->end = vector->end + 4;
        return;
    }
    CallThis<void, char*, void**, void*, uint32_t, uint32_t>(addr::SC4PtrVector_InsertN, vector, end, value, tag, 1u,
                                                             1u);
}

// Slot 6 (0x00612110, ret 4: the address of a list of occupants).
//
// While removals are queued (the byte at +0x74): every occupant of the list
// is referenced (its slot 1) and appended to the vector at +0x68; nothing
// else. The list's head node is read once.
//
// Else three passes, for the masks of the table at 0x00AA7B40 (0x1F4F: the
// surface networks; 0x10: pipes; 0x80: subways) and the tools of the types
// of the table at 0x00AA7B34 (0, 4, 7). In each pass the list's head node
// is read again, and every occupant is asked for the network occupant
// interface (the answer of QueryInterface is not looked at, only the out
// word, cleared before): a piece whose flags (slot 12) meet the mask is
// appended to a vector of the pass and referenced once more; the reference
// QueryInterface gave is released. When the pass has pieces, the manager's
// own slot 5 gives the tool of the type (the kept one, second argument 0);
// it is referenced when not null, and then without a test: Init (slot 3),
// slot 50 with the address of the vector, Shutdown (slot 4), Release. The
// vector's memory is freed; the references of its pieces are not given back
// here.
//
// The game's EAX is what the last call left: nothing is returned.
void __fastcall DestroyNetworkOccupants(char* self, void*, ListNode** list)
{
    static bool logged = false;
    FirstCall("DestroyNetworkOccupants", logged);
    if (At<uint8_t>(self, field::kQueueing)) {
        ListNode* const head = *list;
        Vector* const queue = reinterpret_cast<Vector*>(self + field::kQueue);
        for (ListNode* node = head->next; node != head; node = node->next) {
            AddRefOf(node->value);
            Append(queue, &node->value, &list);
        }
        return;
    }
    for (uint32_t pass = 0; pass < kRemovalPasses; ++pass) {
        ListNode* const head = *list;
        const uint32_t mask = Global<uint32_t>(addr::cSC4NetworkManager_RemovalMasks + 4 * pass);
        Vector pieces = {nullptr, nullptr, nullptr};
        for (ListNode* node = head->next; node != head; node = node->next) {
            void* piece = nullptr;
            Virtual<uint8_t>(node->value, 0x0, kIidNetworkOccupant, &piece);
            if (!piece)
                continue;
            if (mask & Virtual<uint32_t>(piece, piece_slot::kGetNetworkFlag)) {
                void* value = piece;
                Append(&pieces, &value, &list);
                AddRefOf(piece);
            }
            ReleaseOf(piece);
        }
        if (pieces.begin != pieces.end) {
            void* const tool =
                Virtual<void*>(self, slot::kGetNetworkTool,
                               Global<uint32_t>(addr::cSC4NetworkManager_RemovalToolTypes + 4 * pass), 0u);
            if (tool)
                AddRefOf(tool);
            Virtual<void>(tool, tool_slot::kInit);
            Virtual<void>(tool, tool_slot::kDestroyOccupantsAndResolve, &pieces);
            Virtual<void>(tool, tool_slot::kShutdown);
            ReleaseOf(tool);
        }
        if (pieces.begin)
            FreeMemory(pieces.begin);
    }
}

// Slot 7 (0x00613450, ret 4: a byte): the queueing switch. Nothing when the
// byte at +0x74 has the value already. Else it is stored; switching on does
// no more. Switching off with occupants in the vector at +0x68: the vector
// is sorted by address (0x006132E0, at its address); a list is made (a head
// node of the list allocator cached at 0x00B207E4); the vector is walked
// with its bounds as read after the sort: an occupant equal to the one
// before it is released (the queue held a reference for each time it was
// queued), another one gets a node at the list's end, which takes over the
// queue's reference. The comparison starts from null, so a null first entry
// would be released through a null pointer, as in the game. The vector is
// emptied (the game's erase of the whole range: the branch that would move
// a tail compares the end with itself and is never taken). Then the list
// goes to the manager's own slot 6 (which is not queueing any more) and to
// the occupant manager's slot 48, and is destroyed (0x00413FE0).
void __fastcall QueueRemovals(char* self, void*, uint32_t value)
{
    static bool logged = false;
    FirstCall("QueueRemovals", logged);
    const uint8_t queueing = static_cast<uint8_t>(value);
    if (At<uint8_t>(self, field::kQueueing) == queueing)
        return;
    At<uint8_t>(self, field::kQueueing) = queueing;
    if (queueing)
        return;
    Vector* const queue = reinterpret_cast<Vector*>(self + field::kQueue);
    if (queue->begin == queue->end)
        return;
    CallCdecl<void, char*, char*>(addr::cSC4NetworkManager_SortPointers, queue->begin, queue->end);
    ListNode* list = nullptr;
    ListNode* const head = Virtual<ListNode*>(ListNodeAllocator(), allocator_slot::kAllocate, 0xCu);
    head->next = head;
    head->previous = head;
    char* at = queue->begin;
    list = head;
    char* const end = queue->end;
    void* before = nullptr;
    for (; at != end; at += 4) {
        void* const occupant = At<void*>(at, 0);
        if (occupant == before) {
            ReleaseOf(occupant);
            continue;
        }
        LinkLast(list, Virtual<ListNode*>(ListNodeAllocator(), allocator_slot::kAllocate, 0xCu), occupant);
        before = occupant;
    }
    queue->end = queue->begin;
    Virtual<void>(self, slot::kDestroyNetworkOccupants, &list);
    Virtual<void>(Occupants(), occupants_slot::kReleaseOccupantList, &list);
    CallThis<void>(addr::SC4List_Destroy2, &list);
}

// Slot 8 (0x006110B0, ret 0xC: a network tool, an occupant, the address of
// a byte; `this` is not read; the tool's 0x0062B213 is its caller). AL 0
// for a null occupant, for one whose flags (slot 33) have bit 0 clear, and
// for one that does not answer the network occupant interface.
//
// A piece without the flag 0x100000 (not a part of an intersection of
// several cells) is taken out of the occupant manager (slot 21: the
// occupant, 0, 0) and AL is that call's answer; the byte is not written.
//
// A piece with the flag: its cell (slot 59), the tool's 0x00633220 with the
// key (z << 16) + x, then the occupant manager's slot 21; when that says
// no, AL 0. Else the byte at the tool's +0x50 is set, the tool's slot 72 is
// called with what 0x00633220 answered, a record of three words (0, a byte
// 0, 0; the three bytes after the byte are the game's stack: zero here) and
// 0; the caller's byte is set to 1 and AL is 1. The piece QueryInterface
// gave is released on every path after it.
//
// The game keeps the cell's z in its occupant argument's place and has x
// uninitialised before slot 59 writes both (zero here).
uint8_t __fastcall RemoveOrReplaceOccupant(void*, void*, char* tool, void* occupant, uint8_t* replaced)
{
    static bool logged = false;
    FirstCall("RemoveOrReplaceOccupant", logged);
    if (!occupant)
        return 0;
    if (!(Virtual<uint32_t>(occupant, occupant_slot::kGetFlags) & 1u))
        return 0;
    void* piece = nullptr;
    if (Virtual<uint8_t>(occupant, 0x0, kIidNetworkOccupant, &piece)) {
        if (!(Virtual<uint32_t>(piece, piece_slot::kGetNetworkFlag) & kFlagIntersectionPart)) {
            const uint8_t removed = Virtual<uint8_t>(Occupants(), occupants_slot::kRemoveOccupant, occupant, 0u, 0u);
            if (piece)
                ReleaseOf(piece);
            return removed;
        }
        uint32_t x = 0, z = 0;
        Virtual<void>(piece, piece_slot::kGetOccupiedCell, &x, &z);
        void* const entry = CallThis<void*, uint32_t>(addr::cSC4NetworkManager_ToolCellEntry, tool, (z << 16) + x);
        if (Virtual<uint8_t>(Occupants(), occupants_slot::kRemoveOccupant, occupant, 0u, 0u)) {
            uint32_t record[3] = {0u, 0u, 0u};
            At<uint8_t>(tool, tool_slot::kFieldReplacing) = 1;
            Virtual<void>(tool, tool_slot::kSlot72, entry, &record[0], 0u);
            *replaced = 1;
            if (piece)
                ReleaseOf(piece);
            return 1;
        }
    }
    if (piece)
        ReleaseOf(piece);
    return 0;
}

// ====================================================================================
// simulators.netmgr.messages: the vector of point vectors (elements of 0xC
// bytes, each a vector of 8-byte points: a cell's x and z)
// ====================================================================================

void CopyConstructPoints(char* to, const void* from)
{
    CallThis<void*, const void*>(addr::SC4PointVector_CopyConstruct, to, from);
}

void AssignPoints(char* to, const void* from)
{
    CallThis<void*, const void*>(addr::cSC4NetworkManager_PointVectorAssign, to, from);
}

// 0x00611BD0 (cdecl: first, last, to, a tag that is not read): every
// element of the range copy constructed (0x007CED60) at `to` and on; the
// construction is skipped for a null place (the game's placement test). EAX
// is the place after the last.
char* __cdecl ConstructPointListsFrom(char* first, char* last, char* to, void*)
{
    static bool logged = false;
    FirstCall("ConstructPointListsFrom", logged);
    for (; first != last; first += kPointVectorSize, to += kPointVectorSize)
        if (to)
            CopyConstructPoints(to, first);
    return to;
}

// 0x00611C00 (cdecl: first, last, the address of a value): the value
// assigned (0x00611B00) to every element of the range. The game's EAX is
// what the last assignment left.
void __cdecl FillPointLists(char* first, char* last, const Vector* value)
{
    static bool logged = false;
    FirstCall("FillPointLists", logged);
    for (; first != last; first += kPointVectorSize)
        AssignPoints(first, value);
}

// 0x00611C30 (cdecl: to, a count, the address of a value): `count` copies
// of the value constructed at `to` and on (skipped for a null place). EAX
// is the place after the last.
char* __cdecl ConstructPointListCopies(char* to, uint32_t count, const Vector* value)
{
    static bool logged = false;
    FirstCall("ConstructPointListCopies", logged);
    for (; count != 0; --count, to += kPointVectorSize)
        if (to)
            CopyConstructPoints(to, value);
    return to;
}

// 0x00611C60 (cdecl: first, last, to, and two words that are not read): the
// range assigned forward, element by element, to `to` and on. EAX is the
// place after the last, or `to` for a count that is not positive.
char* __cdecl AssignPointLists(char* first, char* last, char* to, void*, uint32_t)
{
    static bool logged = false;
    FirstCall("AssignPointLists", logged);
    for (int32_t count = ListCount(first, last); count > 0; --count) {
        AssignPoints(to, first);
        first += kPointVectorSize;
        to += kPointVectorSize;
    }
    return to;
}

// 0x00611CB0 (cdecl: first, last, the end of the target, and two words that
// are not read): the range assigned backward, from its last element to the
// one before the target's end and down. EAX is the lowest place written, or
// the target's end for a count that is not positive.
char* __cdecl AssignPointListsBackward(char* first, char* last, char* toEnd, void*, uint32_t)
{
    static bool logged = false;
    FirstCall("AssignPointListsBackward", logged);
    for (int32_t count = ListCount(first, last); count > 0; --count) {
        last -= kPointVectorSize;
        toEnd -= kPointVectorSize;
        AssignPoints(toEnd, last);
    }
    return toEnd;
}

// 0x006135E0 (ret 8: first, last; the resize is its only caller): the
// elements from `last` to the end are assigned down onto `first` and on
// (0x00611C60: last, the end, first, a tag, 0); the points of the elements
// left over behind them are freed (0x0090CF63, when not null) and the end
// is set there. EAX is `first`.
char* __fastcall ErasePointLists(Vector* lists, void*, char* first, char* last)
{
    static bool logged = false;
    FirstCall("ErasePointLists", logged);
    char* const end = CallCdecl<char*, char*, char*, char*, void*, uint32_t>(
        addr::cSC4NetworkManager_AssignPointLists, last, lists->end, first, &first, 0u);
    char* const old = lists->end;
    for (char* at = end; at != old; at += kPointVectorSize)
        if (void* const points = At<void*>(at, 0))
            FreeMemory(points);
    lists->end = end;
    return first;
}

// 0x00613640 (ret 0x14: where, the address of a value, a tag that is not
// read, a count, and a byte: the insert is at the end; the insert
// 0x00613770 is its only caller): the insert that needs new memory. The new
// capacity is the size plus the larger of the size and the count; 12 bytes
// each from 0x0090CF54 (none for a capacity of 0). The elements before
// `where` are copy constructed into it, then `count` copies of the value
// (the game has the count of 1 apart: the same), then, unless the byte is
// set, the elements from `where` to the end (0x00611BD0, at its address;
// the tag is the address of the byte's place). Every construction is
// skipped for a null place. The old elements' points and the old memory
// are freed (0x004638B0) and the vector's three words set.
void __fastcall InsertPointListsGrowing(Vector* lists, void*, char* where, const Vector* value, void*, uint32_t count,
                                        uint32_t atEnd)
{
    static bool logged = false;
    FirstCall("InsertPointListsGrowing", logged);
    const uint32_t size = static_cast<uint32_t>(ListCount(lists->begin, lists->end));
    const uint32_t capacity = (size < count ? count : size) + size;
    char* const memory =
        capacity != 0 ? CallCdecl<char*, std::size_t>(addr::SC4_stl_allocate, capacity * kPointVectorSize) : nullptr;
    char* to = memory;
    for (char* from = lists->begin; from != where; from += kPointVectorSize, to += kPointVectorSize)
        if (to)
            CopyConstructPoints(to, from);
    for (uint32_t left = count; left != 0; --left, to += kPointVectorSize)
        if (to)
            CopyConstructPoints(to, value);
    if (!static_cast<uint8_t>(atEnd))
        to = CallCdecl<char*, char*, char*, char*, void*>(addr::cSC4NetworkManager_ConstructPointListsFrom, where,
                                                         lists->end, to, &atEnd);
    CallThis<void>(addr::SC4ShortVectorVector_Destroy, lists);
    lists->end = to;
    lists->begin = memory;
    lists->capacity = memory + capacity * kPointVectorSize;
}

// 0x00613770 (ret 0xC: where, a count, the address of a value; the resize
// is its only caller): `count` copies of the value inserted at `where`.
// Nothing for a count of 0. Without room for them (the capacity less the
// end, in elements, unsigned) the growing insert 0x00613640 does it (where,
// the value, a tag, the count, 0). Else a copy of the value is made first
// (0x007CED60), and with `after` the number of elements from `where` to the
// end:
//   - after > count: the last `count` elements are copy constructed behind
//     the end (0x00611BD0), the end moves up by `count`, the elements from
//     `where` to that tail are assigned backward up to the old end
//     (0x00611CB0), and the copy is assigned to the `count` elements at
//     `where` (0x00611C00);
//   - else: `count - after` copies are constructed at the end (0x00611C30)
//     and the end moves up by them; the elements from `where` to the old
//     end are copy constructed there and the end moves up by `after`; the
//     copy is assigned to the elements from `where` to the old end.
// Last the copy's points are freed. The tag the game hands on is the
// address of its count's place.
void __fastcall InsertPointLists(Vector* lists, void*, char* where, uint32_t count, const Vector* value)
{
    static bool logged = false;
    FirstCall("InsertPointLists", logged);
    if (count == 0)
        return;
    if (static_cast<uint32_t>(ListCount(lists->end, lists->capacity)) < count) {
        CallThis<void, char*, const Vector*, void*, uint32_t, uint32_t>(
            addr::cSC4NetworkManager_InsertPointListsGrowing, lists, where, value, &count, count, 0u);
        return;
    }
    Vector copy = {nullptr, nullptr, nullptr};
    CallThis<void*, const Vector*>(addr::SC4PointVector_CopyConstruct, &copy, value);
    char* const end = lists->end;
    const uint32_t after = static_cast<uint32_t>(ListCount(where, end));
    void* const tag = &count;
    if (after > count) {
        const std::size_t bytes = count * kPointVectorSize;
        char* const tail = end - bytes;
        CallCdecl<char*, char*, char*, char*, void*>(addr::cSC4NetworkManager_ConstructPointListsFrom, tail, end, end,
                                                    tag);
        lists->end = lists->end + bytes;
        CallCdecl<char*, char*, char*, char*, void*, uint32_t>(addr::cSC4NetworkManager_AssignPointListsBackward, where,
                                                              tail, end, tag, 0u);
        CallCdecl<void, char*, char*, const Vector*>(addr::cSC4NetworkManager_FillPointLists, where, where + bytes,
                                                     &copy);
    } else {
        const uint32_t more = count - after;
        CallCdecl<char*, char*, uint32_t, const Vector*>(addr::cSC4NetworkManager_ConstructPointListCopies, end, more,
                                                         &copy);
        char* const grown = lists->end + more * kPointVectorSize;
        lists->end = grown;
        CallCdecl<char*, char*, char*, char*, void*>(addr::cSC4NetworkManager_ConstructPointListsFrom, where, end,
                                                    grown, tag);
        lists->end = lists->end + after * kPointVectorSize;
        CallCdecl<void, char*, char*, const Vector*>(addr::cSC4NetworkManager_FillPointLists, where, end, &copy);
    }
    if (copy.begin)
        FreeMemory(copy.begin);
}

// 0x006138B0 (ret 8: a count, the address of a value; 0x00613920 is its
// only caller): the vector resized to `count` elements. Fewer than it has
// (unsigned): the elements from that count on are erased (0x006135E0). Else
// `count` less the size copies of the value are inserted at the end
// (0x00613770, also for a difference of 0, where it does nothing).
void __fastcall ResizePointListsWith(Vector* lists, void*, uint32_t count, const Vector* value)
{
    static bool logged = false;
    FirstCall("ResizePointListsWith", logged);
    const uint32_t size = static_cast<uint32_t>(ListCount(lists->begin, lists->end));
    if (count < size) {
        CallThis<char*, char*, char*>(addr::cSC4NetworkManager_ErasePointLists, lists,
                                      lists->begin + count * kPointVectorSize, lists->end);
        return;
    }
    CallThis<void, char*, uint32_t, const Vector*>(addr::cSC4NetworkManager_InsertPointLists, lists, lists->end,
                                                   count - size, value);
}

// 0x00613920 (ret 4: a count; DoMessage's flush is its only caller, with
// 13): the vector resized with an empty point vector as the value
// (0x006138B0, at its address). The empty vector needs no destruction.
void __fastcall ResizePointLists(Vector* lists, void*, uint32_t count)
{
    static bool logged = false;
    FirstCall("ResizePointLists", logged);
    Vector empty = {nullptr, nullptr, nullptr};
    CallThis<void, uint32_t, const Vector*>(addr::cSC4NetworkManager_ResizePointListsWith, lists, count, &empty);
}

// ====================================================================================
// simulators.netmgr.messages: the callback, RefreshAllPieces, DoMessage
// ====================================================================================

// 0x00610BB0 (cdecl: an occupant and a context that is not read; DoMessage
// hands its address to the occupant manager's slot 45 for the message
// 0x6AB97795): the occupant is asked for the network occupant interface (the
// answer is not looked at, and the piece is used without a test: an
// occupant of the network type always answers); a piece with the flag
// 0x40000 whose occupant has the flag 4 set (slot 32) posts its "changed"
// message (the occupant's slot 15: 0x99EF1145, 0). The piece is released
// when not null. AL is 1: go on.
uint8_t __cdecl OnOccupantPostChanged(void* occupant, void*)
{
    static bool logged = false;
    FirstCall("OnOccupantPostChanged", logged);
    void* piece = nullptr;
    Virtual<uint8_t>(occupant, 0x0, kIidNetworkOccupant, &piece);
    if (Virtual<uint8_t>(piece, piece_slot::kHasNetworkFlag, kFlagCallback) &&
        Virtual<uint8_t>(occupant, occupant_slot::kIsFlagSet, kOccupantFlagCallback))
        Virtual<uint8_t>(occupant, occupant_slot::kPostOccupantMessage, kMessageOccupantChanged, 0u);
    if (piece)
        ReleaseOf(piece);
    return 1;
}

// 0x00612690 (ret 8: two values; `this` is not read; DoMessage's 0xA6B79602
// is its only caller): nothing when the two are equal or there is no
// occupant manager (0x00B43D0C). Else a list is made (a head node of the
// list allocator cached at 0x00B207E4) and filled with every occupant of
// the network type (the occupant manager's slot 37: the list, no bounds,
// the type 0xC772BF98, no limit); each one that answers the network
// occupant interface is told that its lighting needs an update (the piece's
// slot 46, the game's: drawing state) and that reference released; each
// occupant is released; the list is destroyed (0x00413FE0).
void __fastcall RefreshAllPieces(void*, void*, uint32_t first, uint32_t second)
{
    static bool logged = false;
    FirstCall("RefreshAllPieces", logged);
    if (first == second)
        return;
    void* const occupants = Occupants();
    if (!occupants)
        return;
    ListNode* list = nullptr;
    ListNode* const made = Virtual<ListNode*>(ListNodeAllocator(), allocator_slot::kAllocate, 0xCu);
    made->next = made;
    made->previous = made;
    list = made;
    Virtual<uint8_t>(occupants, occupants_slot::kGetByBBoxByType, &list, 0u, 0u, kTypeNetworkOccupant, kNoLimit);
    ListNode* const head = list;
    for (ListNode* node = head->next; node != head; node = node->next) {
        void* const occupant = node->value;
        void* piece = nullptr;
        if (Virtual<uint8_t>(occupant, 0x0, kIidNetworkOccupant, &piece)) {
            Virtual<void>(piece, piece_slot::kMarkLightingUpdateNeeded);
            ReleaseOf(piece);
        }
        ReleaseOf(occupant);
    }
    CallThis<void>(addr::SC4List_Destroy2, &list);
}

// One corner of PieceOffTerrain: the piece's height there (slot 56, stored
// as a float) less the terrain's (slot 13, as it comes from the FPU), and
// whether that is below 0 or above 0.01 (the double at 0x00A92688). A
// difference that is not a number is neither (the game's two tests of the
// status word both fall through for it).
bool OffTerrainAt(void* piece, void* terrain, float x, float z)
{
    const float height = static_cast<float>(Virtual<double>(piece, piece_slot::kGetAltitude, x, z));
    const double above = static_cast<double>(height) - Virtual<double>(terrain, terrain_slot::kGetAltitude, x, z);
    return above < 0.0 || above > kTerrainTolerance;
}

// PieceOffTerrain (0x006105F0; register arguments, so not hooked: this is
// DoMessage's own copy): whether a piece no longer sits on the ground at
// one of the four corners of its cell. The corner's coordinates are the
// cell's times 16 (a shift by 4, then from integer to float) and those plus
// 16.0 (the float at 0x00A8D45C; added and stored as floats). The corners
// are asked in the order (x, z), (x + 16, z), (x, z + 16), (x + 16, z + 16)
// and the first that is off ends it.
bool PieceOffTerrain(void* piece, void* terrain, uint32_t cellX, uint32_t cellZ)
{
    const float x = static_cast<float>(static_cast<int32_t>(cellX << 4));
    const float z = static_cast<float>(static_cast<int32_t>(cellZ << 4));
    if (OffTerrainAt(piece, terrain, x, z))
        return true;
    const float farX = static_cast<float>(static_cast<double>(x) + kCellMetres);
    if (OffTerrainAt(piece, terrain, farX, z))
        return true;
    const float farZ = static_cast<float>(static_cast<double>(z) + kCellMetres);
    if (OffTerrainAt(piece, terrain, x, farZ))
        return true;
    return OffTerrainAt(piece, terrain, farX, farZ);
}

// The reason of a cell change: the message's fourth data word (slot 24),
// asked again each time the game asks.
uint32_t Reason(void* message)
{
    return Virtual<uint32_t>(message, message_slot::kGetData4);
}

// 0xC9DA96E8 / 0xC9DA96E9 and the end of 0x26D31EC1: every piece of the
// vector at +0x24 (its bounds read once) that says it can be lit (slot 62)
// is unlit or lit (slot 61, the game's: drawing state).
void LightManaged(char* self, uint32_t lit)
{
    const Vector* const managed = reinterpret_cast<const Vector*>(self + field::kManaged);
    char* at = managed->begin;
    char* const end = managed->end;
    for (; at != end; at += 4) {
        void* const piece = At<void*>(at, 0);
        if (Virtual<uint8_t>(piece, piece_slot::kCanBeLit))
            Virtual<void>(piece, piece_slot::kSetLit, lit);
    }
}

// The first half of 0x278E6003 (0x00613A1E to 0x00613C89). The cells: the
// message's first data word holds the x range and its second the z range,
// each with the low cell plus one in the high 16 bits and the high cell in
// the low 16. The low cells are raised to 0 and the high cells lowered to
// the city's last cell (the city's slots 93 and 94 less one, the city read
// from 0x00B43CEC before each; signed compares). While batching (+0x12) the
// dirty rectangle at +0x14 grows to hold them (low x, high x, low z, high
// z, each compared and stored by itself); else they are the rectangle.
//
// Then, unless the reason is 5: the occupant manager (0x00B43D0C, held with
// a reference), two lists, and a network filter of the mask 0x1FFF; the
// occupants of the cells (the occupant manager's slot 40: a list, the low
// and the high cell, the filter, no limit) are walked when the call says
// yes. For each the reason is asked again: 4 keeps it; any reason but 3
// keeps pipes and subways (IsOfType 4 and 7, slot 22); a piece that still
// sits on the terrain (PieceOffTerrain with the piece's cell, slot 59, and
// the terrain 0x00B43CF4) is kept; with the reason 6 a piece with the flag
// 0x80000 is kept. A kept occupant is released; another one goes into the
// second list with its reference (0x00467B50). That list goes to the
// manager's own slot 6 and to the occupant manager's slot 48; the filter is
// released, the lists destroyed, the occupant manager released (neither
// tested for null).
void OnCellsChanged(char* self, void* message)
{
    const uint32_t xs = Virtual<uint32_t>(message, message_slot::kGetData1);
    const uint32_t zs = Virtual<uint32_t>(message, message_slot::kGetData2);
    // Low x, low z, high x, high z: the order of the game's frame (+0x34),
    // whose first and third words' addresses the occupant manager gets.
    int32_t cells[4] = {static_cast<int32_t>((xs >> 16) - 1), static_cast<int32_t>((zs >> 16) - 1),
                        static_cast<int32_t>(xs & 0xFFFFu), static_cast<int32_t>(zs & 0xFFFFu)};
    const int32_t lastX = Virtual<int32_t>(Global<void*>(addr::SC4_City), city_slot::kCellCountX) - 1;
    const int32_t lastZ = Virtual<int32_t>(Global<void*>(addr::SC4_City), city_slot::kCellCountZ) - 1;
    if (cells[0] < 0)
        cells[0] = 0;
    if (cells[1] < 0)
        cells[1] = 0;
    if (cells[2] > lastX)
        cells[2] = lastX;
    if (cells[3] > lastZ)
        cells[3] = lastZ;
    int32_t* const rect = &At<int32_t>(self, field::kDirtyRect);
    if (At<uint8_t>(self, field::kBatching)) {
        if (cells[0] < rect[0])
            rect[0] = cells[0];
        if (cells[2] > rect[2])
            rect[2] = cells[2];
        if (cells[1] < rect[1])
            rect[1] = cells[1];
        if (cells[3] > rect[3])
            rect[3] = cells[3];
    } else {
        rect[0] = cells[0];
        rect[2] = cells[2];
        rect[1] = cells[1];
        rect[3] = cells[3];
    }
    if (Reason(message) == kReasonNoRemoval)
        return;

    void* occupants = nullptr;
    CallThis<void*, void*>(addr::SC4RefHolder_SC4RefHolder, &occupants, Occupants());
    ListNode* found = nullptr;
    CallThis<void*>(addr::SC4List_Construct3, &found);
    ListNode* gone = nullptr;
    CallThis<void*>(addr::SC4List_Construct3, &gone);
    void* const filter = MakeFilter(kEveryTypeMask);
    if (Virtual<uint8_t>(occupants, occupants_slot::kGetByCityCells, &found, &cells[0], &cells[2], filter, kNoLimit)) {
        ListNode* const head = found;
        for (ListNode* node = head->next; node != head; node = node->next) {
            void* occupant = node->value;
            char* const piece = PieceOf(occupant);
            bool take = false;
            if (Reason(message) != kReasonKeep &&
                (Reason(message) == kReasonNoUnderground ||
                 (!Virtual<uint8_t>(piece, piece_slot::kIsOfType, kTypePipe) &&
                  !Virtual<uint8_t>(piece, piece_slot::kIsOfType, kTypeSubway)))) {
                uint32_t x = 0, z = 0;
                Virtual<void>(piece, piece_slot::kGetOccupiedCell, &x, &z);
                if (PieceOffTerrain(piece, Global<void*>(addr::SC4_Terrain), x, z))
                    take = Reason(message) != kReasonKeepFlagged ||
                           !Virtual<uint8_t>(piece, piece_slot::kHasNetworkFlag, kFlagKeptByReason6);
            }
            if (take)
                CallThis<void, void**>(addr::SC4List_PushBack, &gone, &occupant);
            else
                ReleaseOf(occupant);
        }
    }
    Virtual<void>(self, slot::kDestroyNetworkOccupants, &gone);
    Virtual<void>(Occupants(), occupants_slot::kReleaseOccupantList, &gone);
    ReleaseOf(filter);
    CallThis<void>(addr::SC4List_Destroy2, &gone);
    CallThis<void>(addr::SC4List_Destroy2, &found);
    ReleaseOf(occupants);
}

// One of the flush's two rebuilds (0x00613E5B and 0x00613EA3): the network
// manager as SC4::GetNetworkManager gives it (the game does not use its own
// `this` here), its kept tool of the type (slot 5, second argument 0), and
// without a test of it: Init (slot 3); a copy of the type's point vector
// (0x007CED60; the game constructs it in the place of the call's first
// three argument words) handed with the list of pieces and the type to the
// tool code 0x006588D0, which owns the copy from there; Shutdown (slot 4).
void RebuildUnderground(Vector* lists, ListNode** pieces, uint32_t type)
{
    void* const manager = CallCdecl<void*>(addr::SC4_GetNetworkManager);
    void* const tool = Virtual<void*>(manager, slot::kGetNetworkTool, type, 0u);
    Virtual<void>(tool, tool_slot::kInit);
    Vector points = {nullptr, nullptr, nullptr};
    CallThis<void*, const void*>(addr::SC4PointVector_CopyConstruct, &points,
                                 lists->begin + type * kPointVectorSize);
    CallThis<void, char*, char*, char*, ListNode**, uint32_t>(addr::cSC4NetworkManager_ToolRebuildPieces, tool,
                                                              points.begin, points.end, points.capacity, pieces, type);
    Virtual<void>(tool, tool_slot::kShutdown);
}

// 0x6A3B0AFD, and the end of 0x278E6003 when not batching (0x00613C94 to
// 0x00613F41): the flush. With a dirty rectangle that is not empty (low x
// <= high x and low z <= high z, signed):
//   - a vector of thirteen point vectors, one per network type (0x00613920
//     with 13, at its address);
//   - the occupant manager held, a list, a copy of the rectangle and a
//     network filter of the mask 0x90 (pipes and subways); the occupants of
//     the rectangle through the occupant manager's slot 40;
//   - when that says yes: two lists (0x004F5A00), and for every occupant and
//     every type 0 to 12 the piece says it is of (slot 22): the piece's cell
//     (slot 59) appended to that type's point vector (0x004467A0), and for
//     the types 4 and 7 the piece referenced and appended to the pipes' or
//     the subways' list (0x00427640); after the types the piece is released
//     (the reference of the occupant manager's list). A list that is not
//     empty is rebuilt (RebuildUnderground), pipes first. The two lists are
//     destroyed, subways first (0x005650A0: their nodes; the pieces'
//     references are not given back here);
//   - the filter released, the list destroyed, the occupant manager
//     released, the vector of point vectors destroyed (0x004638B0), and the
//     rectangle set empty (+0x18 and +0x14 0x7FFFFFFF, +0x1C and +0x20 -1).
// With or without a rectangle the batching byte at +0x12 is cleared.
//
// The cell's two words are the game's frame, not set before slot 59 writes
// them and kept from piece to piece (zero at first here). The byte the two
// lists' constructors get the address of is not read.
void Flush(char* self)
{
    int32_t* const rect = &At<int32_t>(self, field::kDirtyRect);
    if (rect[0] <= rect[2] && rect[1] <= rect[3]) {
        Vector lists = {nullptr, nullptr, nullptr};
        CallThis<void, uint32_t>(addr::cSC4NetworkManager_ResizePointLists, &lists, kTypes);
        void* occupants = nullptr;
        CallThis<void*, void*>(addr::SC4RefHolder_SC4RefHolder, &occupants, Occupants());
        ListNode* found = nullptr;
        CallThis<void*>(addr::SC4List_Construct3, &found);
        int32_t cells[4] = {rect[0], rect[1], rect[2], rect[3]};
        void* const filter = MakeFilter(kUndergroundMask);
        if (Virtual<uint8_t>(occupants, occupants_slot::kGetByCityCells, &found, &cells[0], &cells[2], filter,
                             kNoLimit)) {
            ListNode* const head = found;
            uint8_t tag = 0;
            ListNode* pipes = nullptr;
            CallThis<void*, uint8_t*>(addr::SC4RefList_Construct, &pipes, &tag);
            ListNode* subways = nullptr;
            CallThis<void*, uint8_t*>(addr::SC4RefList_Construct, &subways, &tag);
            uint32_t cell[2] = {0u, 0u};
            for (ListNode* node = found->next; node != head; node = node->next) {
                char* piece = PieceOf(node->value);
                for (uint32_t type = 0; type < kTypes; ++type) {
                    if (!Virtual<uint8_t>(piece, piece_slot::kIsOfType, type))
                        continue;
                    Virtual<void>(piece, piece_slot::kGetOccupiedCell, &cell[0], &cell[1]);
                    CallThis<void, uint32_t*>(addr::SC4PairVector_PushBack, lists.begin + type * kPointVectorSize,
                                              &cell[0]);
                    if (type == kTypePipe) {
                        AddRefOf(piece);
                        CallThis<void, char**>(addr::SC4StlList_PushBack, &pipes, &piece);
                    } else if (type == kTypeSubway) {
                        AddRefOf(piece);
                        CallThis<void, char**>(addr::SC4StlList_PushBack, &subways, &piece);
                    }
                }
                ReleaseOf(piece);
            }
            if (pipes->next != pipes)
                RebuildUnderground(&lists, &pipes, kTypePipe);
            if (subways->next != subways)
                RebuildUnderground(&lists, &subways, kTypeSubway);
            CallThis<void>(addr::SC4PairList_Destroy, &subways);
            CallThis<void>(addr::SC4PairList_Destroy, &pipes);
        }
        ReleaseOf(filter);
        CallThis<void>(addr::SC4List_Destroy2, &found);
        ReleaseOf(occupants);
        CallThis<void>(addr::SC4ShortVectorVector_Destroy, &lists);
        rect[1] = kEmptyLow;
        rect[0] = kEmptyLow;
        rect[2] = kEmptyHigh;
        rect[3] = kEmptyHigh;
    }
    At<uint8_t>(self, field::kBatching) = 0;
}

// 0x99EF1142 and 0x99EF1143: the occupant the message carries (its slot
// 10; nothing for null) asked for the network occupant interface (nothing
// when it does not answer), and the piece whether it is managed globally
// (slot 5).
//   - inserted and managed: the piece is appended to the vector at +0x24
//     (0x007EBD30), which keeps the reference QueryInterface gave;
//   - removed and managed: the piece is looked for in the vector
//     (0x008BC520 with the bounds and the address of the piece); when it is
//     there its reference is released (through the entry), the vector's
//     last entry takes its place (unless it is the last) and the end steps
//     back; then the piece is released;
//   - not managed: the piece is released.
void OnOccupantMoved(char* self, void* message, bool inserted)
{
    void* const occupant = Virtual<void*>(message, message_slot::kGetVoid1);
    void* piece = nullptr;
    if (!occupant)
        return;
    if (!Virtual<uint8_t>(occupant, 0x0, kIidNetworkOccupant, &piece))
        return;
    const bool managed = Virtual<uint8_t>(piece, piece_slot::kIsManagedGlobally) != 0;
    Vector* const pieces = reinterpret_cast<Vector*>(self + field::kManaged);
    if (inserted) {
        if (managed) {
            CallThis<void, void**>(addr::SC4PtrVector_PushBack, pieces, &piece);
            return;
        }
    } else if (managed) {
        char* const end = pieces->end;
        char* const at = CallCdecl<char*, char*, char*, void**>(addr::cSCLua_F8BC520, pieces->begin, end, &piece);
        if (at != end) {
            ReleaseOf(At<void*>(at, 0));
            char* const last = pieces->end - 4;
            if (at != last)
                At<void*>(at, 0) = At<void*>(last, 0);
            pieces->end = pieces->end - 4;
        }
    }
    ReleaseOf(piece);
}

// The message target's slot 3 (0x00613950, ret 4: the message; `this` is
// the object + 4). By the message's type (its slot 4):
//   0x26D31EC2  ShutdownCrews, then the thirteen tool slots assigned null
//               (ReleaseTools with the address of a null), both at their
//               addresses;
//   0x26D31EC1  ProcessRetrofitList (at its address); then when the clock
//               (0x00B43DBC, not tested for null) says night (slot 28) the
//               managed pieces that can be lit are lit;
//   0x278E6003  OnCellsChanged, and unless batching the flush;
//   0x6A3B0AFD  the flush;
//   0x6AB97795  the occupant manager's slot 45 over every occupant of the
//               network type with the callback 0x00610BB0 (its address, a
//               null context, no bounds, the type 0xC772BF98);
//   0x99EF1142, 0x99EF1143  OnOccupantMoved;
//   0xA6B79602  the message's two pointers (slots 10 and 15) each read at
//               +4; when the two words differ RefreshAllPieces with them (at
//               its address);
//   0xC9DA96E8, 0xC9DA96E9  the managed pieces that can be lit are unlit,
//               lit;
//   0xEA3B0AF1  the dirty rectangle set empty and the batching byte set.
// AL is 1 for every message, also for one of another type; the rest of the
// game's EAX is not reproduced.
uint8_t __fastcall DoMessage(char* part, void*, void* message)
{
    static bool logged = false;
    FirstCall("DoMessage", logged);
    char* const self = part - kMessagePart;
    switch (Virtual<uint32_t>(message, message_slot::kGetType)) {
    case kMsgLetGo: {
        CallThis<void>(addr::cSC4NetworkManager_ShutdownCrews, self);
        void* none = nullptr;
        CallCdecl<void, char*, char*, void**>(addr::cSC4NetworkManager_ReleaseTools, self + field::kTools,
                                              self + field::kTools + 4 * kTypes, &none);
        break;
    }
    case kMsgRetrofit:
        CallThis<void>(addr::cSC4NetworkManager_ProcessRetrofitList, self);
        if (Virtual<uint8_t>(Global<void*>(addr::SC4_Clock43DBC), clock_slot::kIsNight))
            LightManaged(self, 1u);
        break;
    case kMsgCellsChanged:
        OnCellsChanged(self, message);
        if (!At<uint8_t>(self, field::kBatching))
            Flush(self);
        break;
    case kMsgBatchEnd:
        Flush(self);
        break;
    case kMsgPostChanged:
        Virtual<uint8_t>(Occupants(), occupants_slot::kIterateByType, addr::cSC4NetworkManager_OnOccupantPostChanged,
                         0u, 0u, 0u, kTypeNetworkOccupant);
        break;
    case kMsgOccupantInserted:
        OnOccupantMoved(self, message, true);
        break;
    case kMsgOccupantRemoved:
        OnOccupantMoved(self, message, false);
        break;
    case kMsgRefresh: {
        char* const first = Virtual<char*>(message, message_slot::kGetVoid1);
        char* const second = Virtual<char*>(message, message_slot::kGetVoid2);
        const uint32_t one = At<uint32_t>(first, 0x4);
        const uint32_t other = At<uint32_t>(second, 0x4);
        if (one != other)
            CallThis<void, uint32_t, uint32_t>(addr::cSC4NetworkManager_RefreshAllPieces, self, one, other);
        break;
    }
    case kMsgUnlight:
        LightManaged(self, 0u);
        break;
    case kMsgLight:
        LightManaged(self, 1u);
        break;
    case kMsgBatchBegin: {
        int32_t* const rect = &At<int32_t>(self, field::kDirtyRect);
        rect[1] = kEmptyLow;
        rect[0] = kEmptyLow;
        rect[2] = kEmptyHigh;
        rect[3] = kEmptyHigh;
        At<uint8_t>(self, field::kBatching) = 1;
        break;
    }
    default:
        break;
    }
    return 1;
}

// ====================================================================================
// Installation
// ====================================================================================

const Detour kMessageDetours[] = {
    OPENSC4_NETMGR_DETOUR(addr::cSC4NetworkManager_DoMessage, DoMessage),
    OPENSC4_NETMGR_DETOUR(addr::cSC4NetworkManager_OnOccupantPostChanged, OnOccupantPostChanged),
    OPENSC4_NETMGR_DETOUR(addr::cSC4NetworkManager_RefreshAllPieces, RefreshAllPieces),
    OPENSC4_NETMGR_DETOUR(addr::cSC4NetworkManager_ResizePointLists, ResizePointLists),
    OPENSC4_NETMGR_DETOUR(addr::cSC4NetworkManager_ResizePointListsWith, ResizePointListsWith),
    OPENSC4_NETMGR_DETOUR(addr::cSC4NetworkManager_InsertPointLists, InsertPointLists),
    OPENSC4_NETMGR_DETOUR(addr::cSC4NetworkManager_InsertPointListsGrowing, InsertPointListsGrowing),
    OPENSC4_NETMGR_DETOUR(addr::cSC4NetworkManager_ErasePointLists, ErasePointLists),
    OPENSC4_NETMGR_DETOUR(addr::cSC4NetworkManager_ConstructPointListsFrom, ConstructPointListsFrom),
    OPENSC4_NETMGR_DETOUR(addr::cSC4NetworkManager_FillPointLists, FillPointLists),
    OPENSC4_NETMGR_DETOUR(addr::cSC4NetworkManager_ConstructPointListCopies, ConstructPointListCopies),
    OPENSC4_NETMGR_DETOUR(addr::cSC4NetworkManager_AssignPointLists, AssignPointLists),
    OPENSC4_NETMGR_DETOUR(addr::cSC4NetworkManager_AssignPointListsBackward, AssignPointListsBackward),
};

const Detour kRemovalDetours[] = {
    OPENSC4_NETMGR_DETOUR(addr::cSC4NetworkManager_DestroyNetworkOccupants, DestroyNetworkOccupants),
    OPENSC4_NETMGR_DETOUR(addr::cSC4NetworkManager_QueueRemovals, QueueRemovals),
    OPENSC4_NETMGR_DETOUR(addr::cSC4NetworkManager_RemoveOrReplaceOccupant, RemoveOrReplaceOccupant),
    OPENSC4_NETMGR_DETOUR(addr::cSC4NetworkManager_SortPointers, SortPointers),
    OPENSC4_NETMGR_DETOUR(addr::cSC4NetworkManager_IntroSortLoop, IntroSortLoop),
};

bool InstallMessages(registry::Mode) { return InstallDetours(kMessageDetours, "messages"); }
bool InstallRemovals(registry::Mode) { return InstallDetours(kRemovalDetours, "removals"); }

const bool g_registered[] = {
    registry::Register({"simulators.netmgr.messages", "simulators/netmgr", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4NetworkManager_DoMessage,
                        "Our network manager messages: DoMessage (eleven messages, PieceOffTerrain inside it), the "
                        "occupant callback, RefreshAllPieces and the ten functions of the flush's vector of point "
                        "vectors (13 detours; src/core/netmgr_messages.cpp)",
                        false, InstallMessages}),
    registry::Register({"simulators.netmgr.removals", "simulators/netmgr", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4NetworkManager_DestroyNetworkOccupants,
                        "Our network manager removals: DestroyNetworkOccupants, QueueRemovals, "
                        "RemoveOrReplaceOccupant and the queue's sort with its loop (5 detours; "
                        "src/core/netmgr_messages.cpp)",
                        false, InstallRemovals}),
};

} // namespace
} // namespace opensc4::netmgr_sim
