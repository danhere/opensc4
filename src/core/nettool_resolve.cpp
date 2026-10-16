// Our cSC4NetworkTool, part t12, second group (build 641; the class map is in
// src/core/nettool_sim.h): slot 50 DestroyOccupantsAndResolve 0x0062A5C0
// (ret 4: a vector of network occupants), the one function of the group
// simulators.nettool.resolve. It is the tool family's slot 50 (the general
// tool's, the underground base's, the pipe, subway and power line tools'
// vtables point at the same code), reached by the network manager's
// DestroyNetworkOccupants 0x00612110 (the bulldozer, the pipe burst and the
// like; one caller is 0x00612269) and by the view controls.
//
// What it does (from the code). The occupants are queued (a deque of 32-word
// blocks; they are not referenced by the queue, each is referenced when
// taken off it and the reference given up at the end of its turn), their
// cells put in a hash set H2 ("the cells being removed"). Then, until the
// queue is empty and the set of changed cells H1 is empty (the outer loop):
//   1. each queued occupant (once: a set S0 of done occupants; twice seen
//     gives its two references back, S1 of touched occupants keeps the
//     reference taken at the pop) is looked at on its four sides (the
//     game's edge tables 0x00AA8390 / 0x00AA83A0): the neighbour's network
//     occupant (the occupant manager's slot 0x80 through the tool's filter
//     +0x364, then the network occupant interface 0xA821EF94) gets the
//     occupant's connections on that side cleared, per network type of the
//     occupant (the table of type flags 0x00AA835C), and is queued, or its
//     cell marked changed in H1, depending on what it is (a network lot
//     0x200000 marks, an immovable occupant 0x400000 is queued unless it
//     is also 0x100000, then its intersection pieces (the manager's slot 12)
//     join S1, else marks). A tunnel's other end is queued; an occupant with
//     0x100000 puts its intersection pieces into S1.
//   2. the world cache is reset; every occupant of S1 gets the message
//     0xAA7A014D and is taken off the city: by the occupant manager's slot
//     0x54 when its cell is in H2, else by the network manager's
//     RemoveOrReplaceOccupant (slot 8; the cell goes into H1 when it
//     replaced one, out of it when not); then it is shut down (slot 4 of its
//     occupant) and released; S1 is emptied.
//   3. the tool's cells to solve (+0x84) and its satisfier (+0x124) are
//     cleared, the solved cells (+0x98) emptied, the satisfier begun
//     (0 to the terrain's slot 0x60 + 0x00AA2010); each cell of H1 and its
//     two neighbours in each direction: the network occupant there fixes the
//     heights of its corners (AddFixedVertex through the active edges of a
//     network lot) and the cell goes into the tool's set (+0x84) unless
//     immovable (0x400000); H1 is cleared.
//   4. SolveTiles(-1, 1); the pieces of the new solution made into a list
//     (slot 55); each cell of the tool's set: its network occupant when it
//     is a network lot is set unusable; one with exactly two network types
//     loses the type with fewer connected sides (that type's connections
//     cleared on the neighbours, which are marked in H1, and the flag
//     cleared) and its cell is marked; any other occupant is queued again;
//     the crew's FinishPieces(list, 1); the list freed.
// The outer loop runs again while H1 or the queue is not empty. At the end
// the replacing byte (+0x50) gets its old value back, H2, H1 and the deque
// are destroyed.
//
// Every function was written from the disassembly. Calls between part
// functions go to the game's addresses, so each detour stands alone.
//
// NAM. None of the guarded functions is in this part and none is called
// (0x0062CD50 is called by slot 49 only). NAM's DLL calls nothing of this
// function; it reads the tool by layout (+0x1C, +0x54, +0x60, +0x98, +0xCC,
// +0x124, +0x188, +0x240, +0x244, +0x2B5): slot 50 writes +0x50, +0x53,
// +0x78 and the satisfier +0x124 and the cell set +0x84 and the solved cells
// +0x98 in place, in the game's layout, through the game's allocator and
// helpers; nothing is cached across a call.
//
// Floats: one. The terrain's slot 0x60 (ST0) plus the double at 0x00AA2010,
// stored as a float for the satisfier's Begin (done in doubles, the sum
// rounded once to float). The heights of the occupants' corners travel as
// bit patterns (never through a float register).
//
// Registers. 0x00623E60 (GetFirstNetworkTypeFromFlags) keeps EDX (the
// game's code at 0x0062B8E9 pushes EDX after the first call: the second
// argument): ours passes the flags explicitly. Every direct caller of the
// slot is a virtual call (the manager, the view controls); the callee-saved
// registers are the game's.
//
// Wrappers: game.bulldoze and game.quiet.demolish (src/core/game_*.cpp)
// detour DemolishRegion 0x00468410 and DoDemolishOccupantEffect 0x004673B0,
// which are not called here; no wrapper tests a return address in
// 0x0062A5C0 to 0x0062C4B0 (the slot is called through the network
// manager's DestroyNetworkOccupants, which neither of them hooks); the
// pipe-burst disaster reaches it through the manager. sim_timers.cpp does
// not cover this range (the crew's callers end at 0x00608C50).
//
// Stack leftovers: slot 118 (SetOneWayDirections) is called with a dword of
// which the original sets only the low byte (the other three are the
// stack's; ours 0); the call to RemoveOrReplaceOccupant passes the tool.
// The set of the third hash set (H3 at the frame's +0x110) is built and
// destroyed and never filled: ours builds it the same.
//
// Calls, each count from the asm's pushes for that call and the callee's
// own `ret N`; slots as byte offsets. A few virtual calls take their
// arguments from pushes made before a nested call (the occupant's slot
// 0xC0 takes none).
#include "core/nettool_occupants.h"
#include "core/nettool_place.h"
#include "core/nettool_sim.h"

#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>

namespace opensc4::nettool_sim {
namespace {

using occupants::EdgeSideDx;
using occupants::EdgeSideDz;
using occupants::ListNode;
using occupants::TypeFlag;

// ====================================================================================
// Layouts
// ====================================================================================

// A hash set (nettool_rul.h's table): the functor word, the bucket vector,
// the count.
namespace hset = rul::table;
struct HashSet {
    uint32_t functor;
    Vector buckets;
    uint32_t count;
};
static_assert(sizeof(HashSet) == 0x14, "a hash set is 0x14 bytes");

// A tree set on the stack: the head node and the count.
struct TreeSet {
    char* head;
    uint32_t count;
};

// STLport's deque of 32-bit items (blocks of 0x80 bytes): the start and the
// finish iterators (cur, first, last, node), the map and its size.
struct Deque {
    uint32_t* startCur;
    uint32_t* startFirst;
    uint32_t* startLast;
    uint32_t** startNode;
    uint32_t* finishCur;
    uint32_t* finishFirst;
    uint32_t* finishLast;
    uint32_t** finishNode;
    uint32_t** map;
    uint32_t mapSize;
};
static_assert(sizeof(Deque) == 0x28, "a deque is 0x28 bytes");
inline constexpr uint32_t kBlockBytes = 0x80;

// The tool's fields this part uses beyond nettool_sim.h's.
namespace rfield {
inline constexpr std::size_t kCellSet = 0x84;     // the cells to solve (a hash set)
inline constexpr std::size_t kSolved = 0x98;      // the solved cells (vector of 12-byte records)
inline constexpr std::size_t kSatisfier = 0x124;  // the vertex height satisfier
inline constexpr std::size_t kVertexStride = 0x248;
} // namespace rfield

// The network occupant's slots (netocc_sim.h has the names).
namespace nslot {
inline constexpr std::size_t kQueryInterface = 0x00;
inline constexpr std::size_t kHasFlag = 0x20, kClearFlag = 0x2C, kGetFlags = 0x30, kSetUsable = 0x50;
inline constexpr std::size_t kGetVertexHeights = 0x88, kSetEdgeTypesFlag = 0x94, kGetEdgeTypesFlag = 0x98;
inline constexpr std::size_t kSetEdgeType = 0x9C, kGetActiveEdges = 0xA0, kAsOccupant = 0xC0;
inline constexpr std::size_t kGetOccupiedCell = 0xEC, kGetOneWayDirections = 0x114, kSetOneWayDirections = 0x118;
inline constexpr std::size_t kOccupantSlot4 = 0x10; // the occupant part's slot 4 (called after the removal)
} // namespace nslot

// The other objects' slots.
namespace xslot2 {
inline constexpr std::size_t kFirstByCell = 0x80, kRemoveOccupant = 0x54; // the occupant manager's
inline constexpr std::size_t kRemoveOrReplace = 0x20, kGetAllIntersectionPieces = 0x30; // the network manager's
inline constexpr std::size_t kTerrainSlot60 = 0x60;                       // the terrain's (ST0)
inline constexpr std::size_t kInsertOccupantsForSolvedCells = 0xDC;       // the tool's (slot 55)
inline constexpr std::size_t kMessageSetType = 0x14, kMessageSetObject = 0x30, kMessageSetSource = 0x84;
inline constexpr std::size_t kServerPost = 0x0C;                          // the message server's
inline constexpr std::size_t kAllocate = 0x0C, kDeallocate = 0x10;        // the list node allocator's
} // namespace xslot2

// Flags, IDs and numbers.
inline constexpr uint32_t kFlagNetworkLot = 0x200000, kFlagImmovable = 0x400000, kFlagIntersection = 0x100000;
inline constexpr uint32_t kFlagTunnel = 0x80000;
inline constexpr uint32_t kIidTunnelOccupant = 0x8A4BD52B;
inline constexpr uint32_t kMessageOccupantGone = 0xAA7A014D;
inline constexpr uint32_t kMessageSize = 0x2C;
inline constexpr uint32_t kOneWayType = 10;
inline constexpr uint32_t kNetworkFlags = 0x1FFF;
inline constexpr uint32_t kTypeCount = 13;
inline constexpr uint32_t kSideCount = 4;
inline constexpr uint32_t kLowCells = 2; // the cells a changed cell fixes the heights of, each side

// ====================================================================================
// Helpers
// ====================================================================================

inline void Free(void* block) { CallCdecl<void, void*>(addr::SC4_Free, block); }
inline char* Allocate(uint32_t bytes) { return CallCdecl<char*, uint32_t>(addr::SC4_stl_allocate, bytes); }

// The hash sets' inlined code: the bucket count from the vector's length (a
// signed shift), the division unsigned.
inline uint32_t BucketCount(const HashSet& set)
{
    return static_cast<uint32_t>(static_cast<int32_t>(set.buckets.end - set.buckets.begin) >> 2);
}

// A hash set built on the stack as the game does: its words cleared, the
// constructor 0x00829BC0 with 100 buckets.
void InitHashSet(HashSet& set)
{
    set.functor = 0;
    set.buckets.begin = set.buckets.end = set.buckets.capacity = nullptr;
    set.count = 0;
    CallThis<void, uint32_t>(addr::SC4Uint32HashSet_Construct, &set, hset::kFirstBuckets);
}

// The set grown for one more and the key inserted (the result is the
// insert's 9-byte pair: a 12-byte buffer).
void InsertKey(HashSet& set, uint32_t key)
{
    CallThis<void, uint32_t>(addr::SC4Uint32HashSet_Resize, &set, set.count + 1);
    uint32_t result[3] = {0, 0, 0};
    CallThis<void*, uint32_t*, const uint32_t*>(addr::SC4Uint32HashSet_Insert, &set, result, &key);
}

// Whether a key is in the set: the bucket key % count, the chain by the key
// at +4.
bool ContainsKey(const HashSet& set, uint32_t key)
{
    char** const buckets = reinterpret_cast<char**>(set.buckets.begin);
    char* node = buckets[key % BucketCount(set)];
    while (node && At<uint32_t>(node, hset::kKey) != key)
        node = At<char*>(node, hset::kNext);
    return node != nullptr;
}

// The first node of a set (its first non-empty bucket), and the next one.
char* FirstNode(const HashSet& set)
{
    const uint32_t count = BucketCount(set);
    char** const buckets = reinterpret_cast<char**>(set.buckets.begin);
    for (uint32_t i = 0; i < count; ++i)
        if (buckets[i])
            return buckets[i];
    return nullptr;
}
char* NextNode(const HashSet& set, char* node)
{
    if (char* const next = At<char*>(node, hset::kNext))
        return next;
    const uint32_t count = BucketCount(set);
    uint32_t index = At<uint32_t>(node, hset::kKey) % count;
    char** const buckets = reinterpret_cast<char**>(set.buckets.begin);
    while (++index < count)
        if (buckets[index])
            return buckets[index];
    return nullptr;
}

// The tool's own cell set has the same layout at its +0x84 (the object
// starts at the functor word).
inline HashSet& ToolCellSet(char* self) { return *reinterpret_cast<HashSet*>(self + rfield::kCellSet); }

// An empty tree set: a head node of 0x14 bytes from the STL allocator.
TreeSet NewTreeSet()
{
    TreeSet set;
    set.head = Allocate(0x14);
    set.count = 0;
    At<uint8_t>(set.head, 0) = 0;
    At<char*>(set.head, tree::kParent) = nullptr;
    At<char*>(set.head, tree::kLeft) = set.head;
    At<char*>(set.head, tree::kRight) = set.head;
    return set;
}

// A tree set emptied as the game inlines clear(): every node of the tree
// (the right subtree through the set's EraseTree 0x004AFAD0, the left one by
// the loop), the head made empty again.
void ClearTreeSet(TreeSet& set)
{
    if (set.count == 0)
        return;
    char* node = At<char*>(set.head, tree::kParent);
    if (node) {
        do {
            CallThis<void, char*>(addr::SC4OccupantSet_EraseTree, &set, At<char*>(node, tree::kRight));
            char* const left = At<char*>(node, tree::kLeft);
            Free(node);
            node = left;
        } while (node);
    }
    At<char*>(set.head, tree::kLeft) = set.head;
    At<char*>(set.head, tree::kParent) = nullptr;
    At<char*>(set.head, tree::kRight) = set.head;
    set.count = 0;
}

// An occupant inserted into a pointer set; the inserted byte (the pair's
// second).
bool InsertPointer(TreeSet& set, void* value)
{
    uint32_t out[2] = {0, 0};
    CallThis<void, uint32_t*, void**>(addr::SC4PtrSet_Insert, &set, out, &value);
    return (out[1] & 0xFF) != 0;
}

// The deque of the game's inlined code. push_back in place when the block
// has room (nothing written at a null cur); else a block is made (the map
// grown when it has fewer than 2 free slots at the back) as the inlined slow
// path does.
void PushBack(Deque& q, uint32_t value)
{
    if (q.finishCur != q.finishLast - 1) {
        if (q.finishCur)
            *q.finishCur = value;
        ++q.finishCur;
        return;
    }
    const int32_t used = static_cast<int32_t>(q.finishNode - q.map);
    if (q.mapSize - static_cast<uint32_t>(used) < 2)
        CallThis<void, uint32_t, uint32_t>(addr::SC4CellDeque_ReserveMapAtBack, &q, 1u, 0u);
    uint32_t** const next = q.finishNode + 1;
    *next = reinterpret_cast<uint32_t*>(Allocate(kBlockBytes));
    if (q.finishCur)
        *q.finishCur = value;
    q.finishNode = next;
    q.finishFirst = *next;
    q.finishLast = q.finishFirst + kBlockBytes / 4;
    q.finishCur = q.finishFirst;
}

// The first loop's push_back: the slow case is the game's PushBackSlow.
void PushBackFirst(Deque& q, uint32_t value)
{
    if (q.finishCur == q.finishLast - 1) {
        CallThis<void, uint32_t*>(addr::SC4Uint32Deque_PushBackSlow, &q, &value);
        return;
    }
    if (q.finishCur)
        *q.finishCur = value;
    ++q.finishCur;
}

// pop_front: the next item; the block freed when the last of it is taken.
void PopFront(Deque& q)
{
    if (q.startCur == q.startLast - 1) {
        if (q.startFirst)
            Free(q.startFirst);
        uint32_t** const next = q.startNode + 1;
        q.startNode = next;
        q.startFirst = *next;
        q.startLast = q.startFirst + kBlockBytes / 4;
        q.startCur = q.startFirst;
        return;
    }
    ++q.startCur;
}

// The occupant manager's first occupant of a cell through the tool's filter.
inline uint8_t FirstByCell(char* self, void** out, uint32_t x, uint32_t z)
{
    return Virtual<uint8_t>(At<void*>(self, field::kOccupants), xslot2::kFirstByCell, out, x, z,
                            At<void*>(self, field::kFilter));
}

// QueryInterface for the network occupant interface.
inline uint8_t AsNetworkOccupant(void* object, void** out)
{
    return Virtual<uint8_t>(object, nslot::kQueryInterface, kIidNetworkOccupant, out);
}

// The state of one call.
struct State {
    char* self;
    Deque q;
    HashSet changed;  // H1: cells whose network changed
    HashSet removing; // H2: the cells of the occupants being removed
    HashSet unused;   // H3: made and destroyed, never filled
    TreeSet done;     // S0: occupants looked at
    TreeSet touched;  // S1: occupants to take off the city
};

// The intersection pieces of an occupant put into S1 (references kept for
// those inserted): a list from the allocator, the manager's slot 12, the
// walk (0x0062AD10 and 0x0062B040).
void TouchIntersectionPieces(State& st, void* occupant)
{
    ListNode* list = Virtual<ListNode*>(ListNodeAllocator(), xslot2::kAllocate, static_cast<uint32_t>(sizeof(ListNode)));
    list->next = list;
    list->previous = list;
    Virtual<void>(CallCdecl<void*>(addr::SC4_GetNetworkManager), xslot2::kGetAllIntersectionPieces, occupant, &list,
                  0u, 0u);
    // The walk and the end read from the list variable after the call (the
    // manager is given its address).
    ListNode* const head = list;
    for (ListNode* node = head->next; node != head; node = node->next) {
        void* const piece = node->value;
        void* networkPiece = nullptr;
        Virtual<uint8_t>(piece, nslot::kQueryInterface, kIidNetworkOccupant, &networkPiece);
        if (networkPiece && !InsertPointer(st.touched, networkPiece))
            ReleaseOf(networkPiece);
        ReleaseOf(piece);
    }
    CallThis<void>(addr::SC4List_Clear2, &list);
    Virtual<void>(ListNodeAllocator(), xslot2::kDeallocate, static_cast<void*>(list));
}

// ====================================================================================
// 1. The queue
// ====================================================================================

// One side of an occupant: the neighbour's connections cleared and the
// neighbour queued or its cell marked (0x0062A930 to 0x0062AEAD).
void LookAtSide(State& st, char* occupant, uint32_t flags, uint32_t cellX, uint32_t cellZ, uint32_t side)
{
    char* const self = st.self;
    const int32_t sideNumber = static_cast<int32_t>(side) - 2;
    const uint32_t shift = 8 * side;
    const uint32_t nz = static_cast<uint32_t>(EdgeSideDz(side)) + cellZ;
    const uint32_t nx = static_cast<uint32_t>(EdgeSideDx(side)) + cellX;
    if (nx >= At<uint32_t>(self, field::kCellsX))
        return;
    if (nz >= At<uint32_t>(self, field::kCellsZ))
        return;
    char* neighbor = nullptr;
    void* found = nullptr;
    if (!FirstByCell(self, &found, nx, nz)) {
        if (found)
            ReleaseOf(found);
        return;
    }
    const uint8_t isNetwork = AsNetworkOccupant(found, reinterpret_cast<void**>(&neighbor));
    if (found)
        ReleaseOf(found);
    if (!isNetwork) {
        if (neighbor)
            ReleaseOf(neighbor);
        return;
    }
    uint8_t changed = 0;
    for (uint32_t type = 0; type < kTypeCount; ++type) {
        const uint32_t typeFlag = TypeFlag(type);
        if (!(flags & typeFlag))
            continue;
        const uint32_t word = Virtual<uint32_t>(occupant, nslot::kGetEdgeTypesFlag, type) >> (shift & 31);
        if (word & 4) {
            // Connected across the side: the neighbour is queued and its edge toward us cleared.
            AddRefOf(neighbor);
            PushBack(st.q, reinterpret_cast<uint32_t>(neighbor));
            Virtual<void>(neighbor, nslot::kSetEdgeType, type, static_cast<uint32_t>(sideNumber) & 3, 0u);
            if (ContainsKey(st.removing, (cellZ << 16) + cellX))
                InsertKey(st.removing, (nz << 16) + nx);
        } else if (static_cast<uint8_t>(word) != 0) {
            const uint32_t sideBits = static_cast<uint32_t>(sideNumber) & 3;
            Virtual<void>(neighbor, nslot::kSetEdgeType, type, sideBits, 0u);
            if (type == kOneWayType) {
                const uint8_t directions = Virtual<uint8_t>(neighbor, nslot::kGetOneWayDirections);
                const uint8_t count = static_cast<uint8_t>(sideBits * 2);
                uint8_t mask = static_cast<uint8_t>(3u << count);
                mask = static_cast<uint8_t>(~mask);
                const uint8_t value = static_cast<uint8_t>((directions & mask) + static_cast<uint8_t>(0u << count));
                Virtual<void>(neighbor, nslot::kSetOneWayDirections, static_cast<uint32_t>(value));
            }
            changed = 1;
            if (Virtual<uint32_t>(neighbor, nslot::kGetEdgeTypesFlag, type) == 0) {
                const uint32_t rest = Virtual<uint32_t>(neighbor, nslot::kGetFlags) & kNetworkFlags;
                if (rest & (rest - 1))
                    Virtual<void>(neighbor, nslot::kClearFlag, typeFlag);
            }
        }
    }
    if (changed) {
        if (Virtual<uint8_t>(neighbor, nslot::kHasFlag, kFlagNetworkLot)) {
            InsertKey(st.changed, (nz << 16) + nx);
        } else if (Virtual<uint8_t>(neighbor, nslot::kHasFlag, kFlagImmovable)) {
            if (Virtual<uint8_t>(neighbor, nslot::kHasFlag, kFlagIntersection)) {
                TouchIntersectionPieces(st, neighbor);
            } else {
                AddRefOf(neighbor);
                PushBack(st.q, reinterpret_cast<uint32_t>(neighbor));
            }
        } else {
            InsertKey(st.changed, (nz << 16) + nx);
        }
    }
    if (neighbor)
        ReleaseOf(neighbor);
}

// The queue emptied (0x0062A81D to 0x0062B110).
void ProcessQueue(State& st)
{
    while (st.q.finishCur != st.q.startCur) {
        char* const occupant = reinterpret_cast<char*>(*st.q.startCur);
        if (occupant)
            AddRefOf(occupant);
        PopFront(st.q);
        const uint32_t flags = Virtual<uint32_t>(occupant, nslot::kGetFlags);
        if (!InsertPointer(st.done, occupant)) {
            ReleaseOf(occupant);
            ReleaseOf(occupant);
            continue;
        }
        if (!InsertPointer(st.touched, occupant))
            ReleaseOf(occupant);
        uint32_t cellX = 0, cellZ = 0;
        Virtual<void>(occupant, nslot::kGetOccupiedCell, &cellX, &cellZ);
        for (uint32_t side = 0; side < kSideCount; ++side)
            LookAtSide(st, occupant, flags, cellX, cellZ, side);
        if (flags & kFlagTunnel) {
            void* tunnel = nullptr;
            Virtual<uint8_t>(occupant, nslot::kQueryInterface, kIidTunnelOccupant, &tunnel);
            char* const other = CallThis<char*>(addr::SC4TunnelOccupant_OtherEnd, tunnel);
            AddRefOf(other);
            PushBack(st.q, reinterpret_cast<uint32_t>(other));
            if (tunnel)
                ReleaseOf(tunnel);
        }
        if (flags & kFlagIntersection)
            TouchIntersectionPieces(st, occupant);
        ReleaseOf(occupant);
    }
}

// ====================================================================================
// 2. The touched occupants taken off the city (0x0062B11A to 0x0062B388)
// ====================================================================================

void RemoveTouched(State& st)
{
    char* const self = st.self;
    CallThis<void>(addr::cSC4NetworkWorldCache_Reset, self + field::kWorldCache);
    char* const end = st.touched.head;
    char* node = At<char*>(end, tree::kLeft);
    while (node != end) {
        char* const occupant = At<char*>(node, tree::kValue);
        void* const server = Global<void*>(addr::SC4_MessageServer);
        char* const raw = CallCdecl<char*, uint32_t>(addr::RZ_operator_new, kMessageSize);
        void* const message = raw ? CallThis<void*>(addr::cGZMessage_cGZMessage, raw) : nullptr;
        if (message)
            AddRefOf(message);
        Virtual<void>(message, xslot2::kMessageSetType, kMessageOccupantGone);
        Virtual<void>(message, xslot2::kMessageSetObject, Virtual<void*>(occupant, nslot::kAsOccupant));
        Virtual<void>(message, xslot2::kMessageSetSource, Virtual<void*>(occupant, nslot::kAsOccupant));
        Virtual<void>(server, xslot2::kServerPost, message);
        ReleaseOf(message);

        uint8_t replaced = 0;
        uint32_t cellX = 0, cellZ = 0;
        Virtual<void>(occupant, nslot::kGetOccupiedCell, &cellX, &cellZ);
        const uint32_t key = (cellZ << 16) + cellX;
        if (!ContainsKey(st.removing, key)) {
            void* const manager = CallCdecl<void*>(addr::SC4_GetNetworkManager);
            Virtual<uint8_t>(manager, xslot2::kRemoveOrReplace, self, Virtual<void*>(occupant, nslot::kAsOccupant),
                             &replaced);
        } else {
            void* const manager = Global<void*>(addr::SC4_NetworkService);
            Virtual<uint8_t>(manager, xslot2::kRemoveOccupant, Virtual<void*>(occupant, nslot::kAsOccupant), 0u, 0u);
        }
        if (replaced) {
            InsertKey(st.changed, key);
        } else {
            CallThis<void, const uint32_t*>(addr::SC4Uint32HashSet_Erase, &st.changed, &key);
        }
        void* const asOccupant = Virtual<void*>(occupant, nslot::kAsOccupant);
        Virtual<void>(asOccupant, nslot::kOccupantSlot4);
        ReleaseOf(occupant);
        // The next node in order (STLport's increment).
        char* next = At<char*>(node, tree::kRight);
        if (next) {
            node = next;
            for (next = At<char*>(node, tree::kLeft); next; next = At<char*>(node, tree::kLeft))
                node = next;
        } else {
            char* parent = At<char*>(node, tree::kParent);
            if (node == At<char*>(parent, tree::kRight)) {
                do {
                    node = parent;
                    parent = At<char*>(parent, tree::kParent);
                } while (node == At<char*>(parent, tree::kRight));
            }
            if (At<char*>(node, tree::kRight) != parent)
                node = parent;
        }
    }
    ClearTreeSet(st.touched);
}

// ====================================================================================
// 3. The heights (0x0062B388 to 0x0062B774)
// ====================================================================================

// One cell of the changed cells' neighbourhood (0x0062B510 to 0x0062B6CB): the
// network occupant there fixes the heights of its corners and the cell goes
// into the tool's set unless the occupant is immovable. `rowKey` is z << 16.
void FixCellHeights(State& st, uint32_t cx, uint32_t cz, uint32_t rowKey)
{
    char* const self = st.self;
    char* const satisfier = self + rfield::kSatisfier;
    char* neighbor = nullptr;
    if (cx < At<uint32_t>(self, field::kCellsX) && cz < At<uint32_t>(self, field::kCellsZ)) {
        void* found = nullptr;
        if (!FirstByCell(self, &found, cx, cz)) {
            if (found)
                ReleaseOf(found);
        } else {
            const uint8_t isNetwork = AsNetworkOccupant(found, reinterpret_cast<void**>(&neighbor));
            if (found)
                ReleaseOf(found);
            if (isNetwork) {
                const uint32_t stride = At<uint32_t>(self, rfield::kVertexStride);
                const uint32_t vertex = stride * cz + cx;
                uint32_t corners = 0xF;
                if (Virtual<uint8_t>(neighbor, nslot::kHasFlag, kFlagNetworkLot)) {
                    const uint8_t active = Virtual<uint8_t>(neighbor, nslot::kGetActiveEdges);
                    if (!(active & 0x3))
                        corners = 0xE;
                    if (!(active & 0x9))
                        corners &= ~2u;
                    if (!(active & 0xC))
                        corners &= ~4u;
                    if (!(active & 0x6))
                        corners &= ~8u;
                }
                uint32_t heights[4] = {0, 0, 0, 0};
                Virtual<void>(neighbor, nslot::kGetVertexHeights, heights);
                if (corners & 1)
                    CallThis<void, uint32_t, uint32_t>(addr::SC4VertexHeightSatisfier_AddFixedVertex, satisfier,
                                                       vertex, heights[0]);
                if (corners & 2)
                    CallThis<void, uint32_t, uint32_t>(addr::SC4VertexHeightSatisfier_AddFixedVertex, satisfier,
                                                       stride + vertex, heights[1]);
                if (corners & 4)
                    CallThis<void, uint32_t, uint32_t>(addr::SC4VertexHeightSatisfier_AddFixedVertex, satisfier,
                                                       stride + vertex + 1, heights[2]);
                if (corners & 8)
                    CallThis<void, uint32_t, uint32_t>(addr::SC4VertexHeightSatisfier_AddFixedVertex, satisfier,
                                                       vertex + 1, heights[3]);
                if (!Virtual<uint8_t>(neighbor, nslot::kHasFlag, kFlagImmovable))
                    InsertKey(ToolCellSet(self), cx + rowKey);
            }
        }
    }
    if (neighbor)
        ReleaseOf(neighbor);
}

void FixHeights(State& st)
{
    char* const self = st.self;
    CallThis<void>(addr::SC4Uint32HashSet_Clear, &ToolCellSet(self));
    char* const satisfier = self + rfield::kSatisfier;
    CallThis<void>(addr::SC4VertexHeightSatisfier_Reset, satisfier);
    // The solved cells emptied: the game erases the empty range [end, end)
    // from [begin, end), which only moves the end to the begin.
    {
        Vector& solved = At<Vector>(self, rfield::kSolved);
        char* const begin = solved.begin;
        solved.end = begin;
    }
    const double headroom = Global<double>(addr::cSC4NetworkTool_HeightPassHeadroom);
    const float high =
        static_cast<float>(Virtual<double>(At<void*>(self, field::kTerrain), xslot2::kTerrainSlot60) + headroom);
    CallThis<uint8_t, float, float>(addr::SC4VertexHeightSatisfier_Begin, satisfier, 0.0f, high);

    for (char* node = FirstNode(st.changed); node; node = NextNode(st.changed, node)) {
        const uint32_t key = At<uint32_t>(node, hset::kKey);
        const uint32_t x = key & 0xFFFF, z = key >> 16;
        const uint32_t cellsX = At<uint32_t>(self, field::kCellsX), cellsZ = At<uint32_t>(self, field::kCellsZ);
        const uint32_t xLow = x > kLowCells ? x - kLowCells : 0;
        uint32_t xHigh = x + kLowCells;
        if (xHigh >= cellsX)
            xHigh = cellsX - 1;
        const uint32_t zLow = z > kLowCells ? z - kLowCells : 0;
        uint32_t zHigh = z + kLowCells;
        if (zHigh >= cellsZ)
            zHigh = cellsZ - 1;
        if (zLow > zHigh)
            continue;
        uint32_t rowKey = zLow << 16;
        uint32_t cz = zLow;
        do {
            if (xLow <= xHigh) {
                uint32_t cx = xLow;
                do {
                    FixCellHeights(st, cx, cz, rowKey);
                } while (++cx <= xHigh);
            }
            ++cz;
            rowKey += 0x10000;
        } while (cz <= zHigh);
    }
    CallThis<void>(addr::SC4Uint32HashSet_Clear, &st.changed);
}

// ====================================================================================
// 4. The solution (0x0062B774 to 0x0062BC65)
// ====================================================================================

// The number of sides with a connection in an edge word (a byte each; the
// low three bits).
inline uint32_t ConnectedSides(uint32_t word)
{
    uint32_t v = (word & 0x07070707u) + 0x07070707u;
    v &= 0x08080808u;
    v *= 0x01010101u;
    return v >> 0x1B;
}

// An occupant of two network types loses the type with fewer connected sides
// (0x0062B8D8 to 0x0062BB56); true when it was dealt with (false: queued again).
bool DropWeakerType(State& st, char* occupant, uint32_t cellX, uint32_t cellZ, uint32_t low, uint32_t rest)
{
    char* const self = st.self;
    const uint32_t firstType = CallCdecl<uint32_t, uint32_t>(addr::cSC4NetworkTool_GetFirstNetworkTypeFromFlags, low);
    const uint32_t secondType = CallCdecl<uint32_t, uint32_t>(addr::cSC4NetworkTool_GetFirstNetworkTypeFromFlags, rest);
    const uint32_t firstWord = Virtual<uint32_t>(occupant, nslot::kGetEdgeTypesFlag, firstType);
    const uint32_t secondWord = Virtual<uint32_t>(occupant, nslot::kGetEdgeTypesFlag, secondType);
    const uint32_t firstCount = ConnectedSides(firstWord);
    const uint32_t secondCount = ConnectedSides(secondWord);
    if (firstCount == secondCount)
        return false;
    // The type with fewer connected sides goes.
    const bool firstGoes = firstCount < secondCount;
    const uint32_t type = firstGoes ? firstType : secondType;
    const uint32_t word = firstGoes ? firstWord : secondWord;
    const uint32_t flag = firstGoes ? low : rest;
    for (uint32_t side = 0; side < kSideCount; ++side) {
        if (!(word & (0xFFu << (8 * side))))
            continue;
        const uint32_t nx = static_cast<uint32_t>(EdgeSideDx(side)) + cellX;
        const uint32_t nz = static_cast<uint32_t>(EdgeSideDz(side)) + cellZ;
        if (nx >= At<uint32_t>(self, field::kCellsX))
            continue;
        if (nz >= At<uint32_t>(self, field::kCellsZ))
            continue;
        char* neighbor = nullptr;
        void* around = nullptr;
        if (!FirstByCell(self, &around, nx, nz)) {
            if (around)
                ReleaseOf(around);
        } else {
            const uint8_t isNetwork = AsNetworkOccupant(around, reinterpret_cast<void**>(&neighbor));
            if (around)
                ReleaseOf(around);
            if (isNetwork) {
                Virtual<void>(neighbor, nslot::kSetEdgeType, type, (side - 2) & 3, 0u);
                if (Virtual<uint32_t>(neighbor, nslot::kGetEdgeTypesFlag, type) == 0) {
                    const uint32_t has = Virtual<uint32_t>(neighbor, nslot::kGetFlags) & kNetworkFlags;
                    if (has & (has - 1))
                        Virtual<void>(neighbor, nslot::kClearFlag, flag);
                }
                InsertKey(st.changed, (nz << 16) + nx);
            }
        }
        if (neighbor)
            ReleaseOf(neighbor);
    }
    Virtual<void>(occupant, nslot::kSetEdgeTypesFlag, type, 0u);
    Virtual<void>(occupant, nslot::kClearFlag, flag);
    InsertKey(st.changed, (cellZ << 16) + cellX);
    return true;
}

void SolveAndFinish(State& st)
{
    char* const self = st.self;
    At<uint32_t>(self, field::kValidSteps) = 0;
    CallThis<uint32_t, int32_t, uint32_t>(addr::cSC4NetworkTool_SolveTiles, self, -1, 1u);
    // The list of the pieces made (a head node of its own).
    ListNode* pieces = reinterpret_cast<ListNode*>(Allocate(0xC));
    pieces->next = pieces;
    pieces->previous = pieces;
    Virtual<void>(self, xslot2::kInsertOccupantsForSolvedCells, self + rfield::kSolved, &pieces, 0u);

    HashSet& cells = ToolCellSet(self);
    for (char* node = FirstNode(cells); node; node = NextNode(cells, node)) {
        const uint32_t key = At<uint32_t>(node, hset::kKey);
        const uint32_t x = key & 0xFFFF, z = key >> 16;
        void* found = nullptr;
        if (!FirstByCell(self, &found, x, z)) {
            if (found)
                ReleaseOf(found);
            continue;
        }
        // The occupant manager gives the occupant part; the network occupant
        // starts 4 bytes before it.
        char* const occupant = found ? static_cast<char*>(found) - 4 : nullptr;
        if (Virtual<uint8_t>(occupant, nslot::kHasFlag, kFlagNetworkLot)) {
            Virtual<void>(occupant, nslot::kSetUsable, 0u);
            if (found)
                ReleaseOf(found);
            continue;
        }
        uint32_t cellX = 0, cellZ = 0;
        Virtual<void>(occupant, nslot::kGetOccupiedCell, &cellX, &cellZ);
        const uint32_t flags = Virtual<uint32_t>(occupant, nslot::kGetFlags) & kNetworkFlags;
        const uint32_t rest = flags & (flags - 1);
        const uint32_t low = rest ^ flags;
        bool dealtWith = false;
        if (rest != 0 && (rest & (rest - 1)) == 0)
            dealtWith = DropWeakerType(st, occupant, cellX, cellZ, low, rest);
        if (!dealtWith) {
            AddRefOf(occupant);
            PushBack(st.q, reinterpret_cast<uint32_t>(occupant));
        }
        if (found)
            ReleaseOf(found);
    }

    CallCdecl<void, void*, uint32_t>(addr::cSC4NetworkConstructionCrew_FinishPieces, &pieces, 1u);
    for (ListNode* link = pieces->next; link != pieces;) {
        ListNode* const current = link;
        link = link->next;
        Free(current);
    }
    pieces->next = pieces;
    pieces->previous = pieces;
    Free(pieces);
}

// ====================================================================================
// Slot 50
// ====================================================================================

// The first prime of the game's table (0x00AA829B0, 28 words) that is 100 or
// more, found as the inlined lower bound does, or the table's last
// answer 0xFFFFFFFB when none is.
uint32_t FirstBucketPrime()
{
    const uint32_t* position = reinterpret_cast<const uint32_t*>(addr::SC4HashTable_Primes);
    int32_t length = 0x1C;
    do {
        const int32_t half = length >> 1;
        if (position[half] < hset::kFirstBuckets) {
            position = position + half + 1;
            length += -1 - half;
        } else {
            length = half;
        }
    } while (length > 0);
    if (position == reinterpret_cast<const uint32_t*>(addr::SC4HashTable_PrimesEnd))
        return 0xFFFFFFFBu;
    return *position;
}

void __fastcall DestroyOccupantsAndResolve(char* self, void*, Vector* list)
{
    static bool logged = false;
    FirstCall("DestroyOccupantsAndResolve", logged);
    if (list->begin == list->end)
        return;
    const uint8_t oldReplacing = At<uint8_t>(self, field::kReplacing);
    At<uint8_t>(self, field::kReload) = 1;
    At<uint8_t>(self, field::kReplacing) = 1;

    State st;
    st.self = self;
    st.q = Deque{};
    CallThis<void, uint32_t>(addr::SC4CellDeque_Construct, &st.q, 0u);
    InitHashSet(st.changed);
    InitHashSet(st.removing);
    for (uint32_t* item = reinterpret_cast<uint32_t*>(list->begin), * const end = reinterpret_cast<uint32_t*>(list->end);
         item != end; ++item) {
        char* const occupant = reinterpret_cast<char*>(*item);
        PushBackFirst(st.q, reinterpret_cast<uint32_t>(occupant));
        uint32_t x = 0, z = 0;
        Virtual<void>(occupant, nslot::kGetOccupiedCell, &x, &z);
        InsertKey(st.removing, (z << 16) + x);
    }

    do {
        st.touched = NewTreeSet();
        st.done = NewTreeSet();
        // The third hash set: bucket vector of the first prime of 100 or more, never filled.
        st.unused.functor = 0;
        st.unused.buckets.begin = st.unused.buckets.end = st.unused.buckets.capacity = nullptr;
        st.unused.count = 0;
        const uint32_t prime = FirstBucketPrime();
        CallThis<void, uint32_t>(addr::PtrVector_Reserve, &st.unused.buckets, prime);
        const uint32_t zero = 0;
        CallThis<void, char*, uint32_t, const uint32_t*>(addr::SC4PtrVector_FillInsert, &st.unused.buckets,
                                                          st.unused.buckets.end, prime, &zero);
        st.unused.count = 0;

        ProcessQueue(st);
        RemoveTouched(st);
        FixHeights(st);
        SolveAndFinish(st);

        CallThis<void>(addr::SC4Uint32HashSet_Clear, &st.unused);
        if (st.unused.buckets.begin)
            Free(st.unused.buckets.begin);
        ClearTreeSet(st.done);
        if (st.done.head)
            Free(st.done.head);
        ClearTreeSet(st.touched);
        if (st.touched.head)
            Free(st.touched.head);
    } while (st.changed.count != 0 || st.q.finishCur != st.q.startCur);

    At<uint8_t>(self, field::kReplacing) = oldReplacing;
    CallThis<void>(addr::SC4Uint32HashSet_Clear, &st.removing);
    if (st.removing.buckets.begin)
        Free(st.removing.buckets.begin);
    CallThis<void>(addr::SC4Uint32HashSet_Clear, &st.changed);
    if (st.changed.buckets.begin)
        Free(st.changed.buckets.begin);
    if (uint32_t** const map = st.q.map) {
        uint32_t** const last = st.q.finishNode + 1;
        for (uint32_t** block = st.q.startNode; block < last; ++block)
            if (*block)
                Free(*block);
        Free(map);
    }
}

// ====================================================================================
// Installation
// ====================================================================================

#define OPENSC4_DETOUR(address, fn) OPENSC4_NETTOOL_DETOUR(address, fn)

const Detour kResolve[] = {
    OPENSC4_DETOUR(addr::cSC4NetworkTool_DestroyOccupantsAndResolve, DestroyOccupantsAndResolve),
};

#undef OPENSC4_DETOUR

bool InstallResolve(registry::Mode) { return InstallDetours(kResolve, "resolve"); }

const bool g_registered[] = {
    registry::Register({"simulators.nettool.resolve", "simulators/nettool", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4NetworkTool_DestroyOccupantsAndResolve,
                        "Our network tool's slot 50 DestroyOccupantsAndResolve (the occupants taken off, the "
                        "cells they leave solved again; 1 detour; src/core/nettool_resolve.cpp)",
                        false, InstallResolve}),
};

} // namespace
} // namespace opensc4::nettool_sim
