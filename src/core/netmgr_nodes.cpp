// Our cSC4NetworkManager, third part, second half (build 641; the class map,
// the vtables in their true order and the fields are in
// src/core/netmgr_sim.h): the retrofit pass and the lots' connection nodes.
// ProcessRetrofitList (not virtual; DoMessage calls it for the message
// 0x26D31EC1, a city was loaded), CreateConnectionNode (slot 20) and
// AutoconnectLotNodes (slot 21). Each is a detour at the game's address, and
// every call from one of them to another function of the class goes through
// the game's address or the vtable, so a group that is off, or a function on
// the skip list, is the game's.
//
// Every function was written from the disassembly, instruction by
// instruction; the stack offsets of ProcessRetrofitList's frame (0x94 bytes
// of locals and four saved registers, with up to five pushes pending) were
// each worked out twice.
//
// The detours are plain functions, so they clobber ECX and EDX. The game's
// ProcessRetrofitList keeps both when the list is empty (it reads the list's
// head node into EAX and returns); its only caller, DoMessage's 0x006139D1,
// loads ECX from 0x00B43DBC next. CreateConnectionNode and
// AutoconnectLotNodes have no path that keeps ECX and EDX (both start with
// calls) and are reached through the vtable only (the lot developer's
// 0x006C9E89 and 0x006C9F30, which load their registers again).
//
// Left as the game's and called at their addresses or through vtables: the
// network tools (the constructor 0x0062C5F0 and the tools' slots 3, 4, 7, 8
// and 74), the network filter's constructor 0x0061F530, the occupant classes'
// functions (every piece slot; the pre-built piece's SetUp 0x0061D120), the
// resource manager, the lot manager, the lot and its building, the list
// helpers 0x00413FE0 and 0x00413E60, and of this class GetNetworkTool (slot
// 5), GetAllIntersectionPieces (slot 12) and the retrofit list's clear
// 0x00611A20.
//
// Calls, each count from the asm's pushes for that call and the callee's own
// `ret N`:
//   - operator new 0x005E55E0 (cdecl, one: `add esp, 4`); the tool's
//     constructor 0x0062C5F0 one (ret 4); the filter's 0x0061F530 one (ret
//     4); AddRef and Release none; QueryInterface two;
//   - a piece's slots (the vtable 0x00AA8080): 0x18 CheckForRetrofit none
//     (0x00619180), 0x1C SetRetrofitFlag one (0x0061A120, ret 4), 0x20
//     HasNetworkFlag one (0x00604D10, ret 4), 0x28 SetNetworkFlag and 0x2C
//     ClearNetworkFlag one (0x00619EC0, 0x00619F70, ret 4), 0x34
//     GetConnectionNodeEdgeTypes two (0x00619110, ret 8), 0x38
//     SetConnectionNodeEdgeTypes two (0x0061A020, ret 8), 0x40
//     SetConnectionPathIDOverride one (0x0061A090, ret 4), 0x50 SetUsable one
//     (0x00604D90, a jump to slot 10 or 11 with its argument replaced), 0x5C
//     the piece ID none (0x00459960: the word at +0xFC), 0x68
//     GetRotationAndFlip none (0x00604DE0: AL), 0x78 two (0x0061B490, ret 8:
//     the piece ID and the rotation byte), 0x90 SetVertexHeights one
//     (0x00619650, ret 4), 0xA0 GetActiveEdges none (0x0061AE90), 0xCC
//     SetPathIDOverride one (0x006197D0, ret 4), 0xD0 GetPathIDOverride none
//     (0x006197E0: the word at +0x164), 0xEC GetOccupiedCell two (0x0061B010,
//     ret 8), 0x11C GetComplexPieceOrigin two (0x00619970, ret 8);
//   - an occupant's slots: 0x20 GetPosition one, 0x3C PostOccupantMessage two
//     (ret 8);
//   - the occupant manager's (0x00B43D0C) slot 0x80 four (out, x, z, filter;
//     0x0049E690, ret 0x10);
//   - the lot manager's (0x00B43D08) slot 0x18 three (x, z, 0; 0x0072B410,
//     ret 0xC); the lot's slot 0x38 one (0x006BF320, ret 4) and 0xC0 none
//     (0x006BFA30); the building's slot 0xC none (nothing pushed);
//   - the resource manager's (0x00B43CA8) slot 0xC five (five pushes: the
//     key's address, 0xA52160F5, the out word's address, 0, 0);
//   - the pre-built piece's SetUp 0x0061D120 three (ret 0xC);
//   - a tool's slots (the vtable 0x00AA8748): 0xC Init and 0x10 Shutdown
//     none, 0x1C eight (0x00629210, ret 0x20), 0x20 five (0x00624FD0, ret
//     0x14), 0x128 one (0x0062C990, ret 4; the answer in ST0);
//   - this class: slot 0x14 GetNetworkTool two (ret 8), slot 0x30
//     GetAllIntersectionPieces four (ret 0x10), the list's clear 0x00611A20
//     none;
//   - the list's destructor 0x00413FE0 and clear 0x00413E60 none; the node
//     allocator's slot 0xC one (the size) and 0x10 one (the node).
#include "core/netmgr_sim.h"

#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>

namespace opensc4::netmgr_sim {
namespace {

// The occupant manager (0x00B43D0C; the address entry is called
// SC4::NetworkService by an old mistake).
void* Occupants()
{
    return Global<void*>(addr::SC4_NetworkService);
}

// A network type's flag: the word of the table at 0x00AA79E4. The table has
// thirteen words; AutoconnectLotNodes reads it with 13 for a piece of no
// type, which is the word behind it (the first x step of the neighbour table
// at 0x00AA7A18, -1), as the game does.
uint32_t TypeFlag(uint32_t type)
{
    return Global<uint32_t>(addr::cSC4NetworkManager_TypeFlags + type * 4u);
}

// A new network filter of a mask (0x10 bytes, constructor 0x0061F530),
// referenced when it is not null: what the three functions have inline. Null
// when operator new gives none.
void* MakeMaskFilter(uint32_t mask)
{
    void* filter = nullptr;
    if (void* const raw = CallCdecl<void*, std::size_t>(addr::SC4_operator_new, kNetworkFilterSize))
        filter = CallThis<void*, uint32_t>(addr::cSC4NetworkManager_NetworkFilterConstruct, raw, mask);
    if (filter)
        AddRefOf(filter);
    return filter;
}

// ====================================================================================
// simulators.netmgr.retrofit: ProcessRetrofitList
// ====================================================================================

// A cell's offset from the structure's origin, turned by the origin piece's
// rotation and flip byte (0x00612AB8 to 0x00612AF2, and again 0x00612BBF to
// 0x00612BDF): with bit 7 the first is negated; with bit 1 both are; with
// bit 0 the first becomes the second and the second the first negated. In
// that order, in 32-bit arithmetic.
void TurnOffset(uint8_t turn, uint32_t& a, uint32_t& b)
{
    if (turn & kTurnFlip)
        a = 0u - a;
    if (turn & kTurnHalf) {
        a = 0u - a;
        b = 0u - b;
    }
    if (turn & kTurnQuarter) {
        const uint32_t was = a;
        a = b;
        b = 0u - was;
    }
}

// One listed piece that still has the retrofit property (0x006128E6 to
// 0x00612DAB; the caller releases the piece after it on every path).
//
// A list is made (a head node of the list allocator cached at 0x00B207E4)
// and filled with the pieces of the piece's structure by the manager's own
// slot 12 (the piece, the list, no occupant manager, no other piece). The
// piece's complex piece origin (slot 71) is asked; without one the list is
// destroyed (0x00413FE0) and that is all. The first occupant the filter
// takes on the origin's cell (the occupant manager's slot 32) is looked up;
// without one, the same (an occupant the call stored anyway is released).
// That occupant is asked for the network occupant interface (the answer is
// not looked at; the piece is used without a test), and its path ID override
// (slot 52) is looked for among the first words of the eleven pairs at
// 0x00AA7B50; without a match, or with a second word of 0, the origin piece
// and the occupant are released and the list destroyed.
//
// On those three ways out the list's entries, each of which holds a
// reference (slot 12 gives them), are not released: 0x00413FE0 only frees
// the nodes. The game's leak; ours is the same.
//
// With a match the second word is the base of the new piece IDs and the
// origin piece's rotation and flip byte (slot 26) turns the offsets. A first
// walk over the list takes the lowest turned offset of each of the two
// coordinates, starting from 0 (signed compares). A second walk gives every
// piece of the list its new state:
//   - the new ID is the base + ((a - lowest a) << 8) + ((b - lowest b) << 4),
//     computed as ((((a - lowest a) << 4) - lowest b + b) << 4) + base;
//   - an occupant that answers the pre-built piece's interface 0x49C1A034:
//     the exemplar of the key (0x6534284A, 0xA8434037, the new ID) is asked
//     of the resource manager (slot 3, interface 0xA52160F5), and when it is
//     there the piece is set up with it (0x0061D120: the exemplar, the
//     address of three zero floats, the byte's low two bits) and the
//     exemplar released;
//   - the piece's path ID override is cleared (slot 51), its ID and rotation
//     set (slot 30: the new ID and the origin's byte as it is), the flags
//     0x2000000 and 0x4000000 cleared and 0x8000000 set, the retrofit
//     property taken off (slot 7 with 0), and its occupant posts its
//     "changed" message (slot 15: 0x99EF1145, 0);
//   - the list's reference of the occupant is released, then the pre-built
//     interface and the piece the QueryInterface gave.
// Then the origin piece and its occupant are released, the list's nodes
// freed (0x00413E60) and its head node given back to the allocator.
//
// Not reproduced, and nothing reads them: the words of a cell and of the
// origin before the slots write them (the game's stack; zero here), the
// exemplar's out word before the resource manager writes it (null here), and
// the three bytes above the rotation byte in the word the game pushes for
// slot 30 (its frame's leftovers; slot 30 stores the low byte and hands the
// word to 0x00619550, which reads the low byte: zero here).
void RetrofitStructure(char* self, void* occupants, void* filter, void* piece)
{
    ListNode* members = nullptr;
    ListNode* const made = Virtual<ListNode*>(ListNodeAllocator(), allocator_slot::kAllocate, 0xCu);
    made->next = made;
    made->previous = made;
    members = made;
    void* const none = nullptr;
    Virtual<uint32_t>(self, slot::kGetAllIntersectionPieces, piece, &members, none, none);
    uint32_t originX = 0, originZ = 0;
    if (!Virtual<uint8_t>(piece, piece_slot::kGetComplexPieceOrigin, &originX, &originZ)) {
        CallThis<void>(addr::SC4List_Destroy2, &members);
        return;
    }
    void* found = nullptr;
    if (!Virtual<uint8_t>(occupants, occupants_slot::kGetFirstByCityCell, &found, originX, originZ, filter)) {
        if (found)
            ReleaseOf(found);
        CallThis<void>(addr::SC4List_Destroy2, &members);
        return;
    }
    void* origin = nullptr;
    Virtual<uint8_t>(found, 0x0, kIidNetworkOccupant, &origin);
    const uint32_t oldId = Virtual<uint32_t>(origin, piece_slot::kGetPathIDOverride);
    const uint8_t turn = Virtual<uint8_t>(origin, piece_slot::kGetRotationAndFlip);
    uint32_t base = 0;
    for (uint32_t pair = 0; pair < kRetrofitPairs; ++pair)
        if (Global<uint32_t>(addr::cSC4NetworkManager_RetrofitPairs + pair * 8u) == oldId) {
            base = Global<uint32_t>(addr::cSC4NetworkManager_RetrofitPairs + pair * 8u + 4u);
            break;
        }
    if (base == 0) {
        if (origin)
            ReleaseOf(origin);
        if (found)
            ReleaseOf(found);
        CallThis<void>(addr::SC4List_Destroy2, &members);
        return;
    }

    ListNode* const head = members;
    int32_t lowA = 0, lowB = 0;
    for (ListNode* node = head->next; node != head; node = node->next) {
        void* member = nullptr;
        Virtual<uint8_t>(node->value, 0x0, kIidNetworkOccupant, &member);
        uint32_t x = 0, z = 0;
        Virtual<void>(member, piece_slot::kGetOccupiedCell, &x, &z);
        uint32_t a = x - originX, b = z - originZ;
        TurnOffset(turn, a, b);
        if (static_cast<int32_t>(a) < lowA)
            lowA = static_cast<int32_t>(a);
        if (static_cast<int32_t>(b) < lowB)
            lowB = static_cast<int32_t>(b);
        if (member)
            ReleaseOf(member);
    }

    for (ListNode* node = head->next; node != head; node = node->next) {
        void* const occupant = node->value;
        void* member = nullptr;
        Virtual<uint8_t>(occupant, 0x0, kIidNetworkOccupant, &member);
        uint32_t x = 0, z = 0;
        Virtual<void>(member, piece_slot::kGetOccupiedCell, &x, &z);
        uint32_t a = x - originX, b = z - originZ;
        TurnOffset(turn, a, b);
        const uint32_t id =
            ((((a - static_cast<uint32_t>(lowA)) << 4) - static_cast<uint32_t>(lowB) + b) << 4) + base;
        void* prebuilt = nullptr;
        if (Virtual<uint8_t>(occupant, 0x0, kIidPrebuilt, &prebuilt)) {
            uint32_t key[3] = {kExemplarType, kPrebuiltExemplarGroup, id};
            void* exemplar = nullptr;
            if (Virtual<uint8_t>(Global<void*>(addr::SC4_ResourceManager), resources_slot::kGetResource, &key[0],
                                 kIidExemplar, &exemplar, 0u, 0u)) {
                float offset[3] = {0.0f, 0.0f, 0.0f};
                CallThis<void, void*, float*, uint32_t>(addr::cSC4PrebuiltNetworkOccupant_SetUp, prebuilt, exemplar,
                                                        &offset[0], static_cast<uint32_t>(turn) & 3u);
                ReleaseOf(exemplar);
            }
        }
        Virtual<void>(member, piece_slot::kSetPathIDOverride, 0u);
        Virtual<void>(member, piece_slot::kSetPieceAndRotation, id, static_cast<uint32_t>(turn));
        Virtual<void>(member, piece_slot::kClearNetworkFlag, kFlagNew1);
        Virtual<void>(member, piece_slot::kClearNetworkFlag, kFlagNew2);
        Virtual<void>(member, piece_slot::kSetNetworkFlag, kFlagRetrofitted);
        Virtual<void>(member, piece_slot::kSetRetrofitFlag, 0u);
        Virtual<uint8_t>(occupant, occupant_slot::kPostOccupantMessage, kMessageOccupantChanged, 0u);
        ReleaseOf(occupant);
        if (prebuilt)
            ReleaseOf(prebuilt);
        if (member)
            ReleaseOf(member);
    }

    if (origin)
        ReleaseOf(origin);
    if (found)
        ReleaseOf(found);
    CallThis<void>(addr::SC4List_Clear2, &members);
    Virtual<void>(ListNodeAllocator(), allocator_slot::kFree, members);
}

// 0x00612820 (no arguments, a plain ret; DoMessage's 0x006139D1 is its only
// caller, for the message 0x26D31EC1): the retrofit pass. The pieces on the
// list at +0x78 are the ones whose record was of an old version: Read gave
// them the retrofit property and OccupantInit handed them to
// AddToRetrofitList. Such a piece is a cell of a structure of several cells
// (a highway interchange) that the old version kept as copies of one piece
// with the whole structure's ID as its path ID override; the pass gives
// every cell the piece ID of its own place in the structure.
//
// Nothing for an empty list (ECX and EDX untouched). Else:
//   - a network tool of its own is made (0x368 bytes, the constructor
//     0x0062C5F0 with type 0) and referenced when not null; nothing but the
//     Release at the end uses it;
//   - a network filter of the mask 0x1F4F is made and referenced;
//   - for every node of the list, in order (the head node read once, a
//     node's link read after the node is done): a piece that says it has the
//     retrofit property (slot 6) goes through RetrofitStructure; then the
//     piece is released (the list's reference);
//   - the list is emptied (0x00611A20, at its address: the nodes back to
//     their allocator), the filter released, then the tool.
// The occupant manager is read from 0x00B43D0C once, before the tool is
// made. The game's EAX is what the last call left: nothing is returned.
void __fastcall ProcessRetrofitList(char* self, void*)
{
    static bool logged = false;
    FirstCall("ProcessRetrofitList", logged);
    ListNode** const list = &At<ListNode*>(self, field::kRetrofit);
    if ((*list)->next == *list)
        return;
    void* const occupants = Occupants();
    void* const tool = MakeTool(0u);
    if (tool)
        AddRefOf(tool);
    void* const filter = MakeMaskFilter(kSurfaceMask);
    ListNode* const head = *list;
    for (ListNode* node = head->next; node != head; node = node->next) {
        void* const piece = node->value;
        if (Virtual<uint8_t>(piece, piece_slot::kCheckForRetrofit))
            RetrofitStructure(self, occupants, filter, piece);
        ReleaseOf(piece);
    }
    CallThis<void>(addr::cSC4NetworkManager_ClearRetrofitList, self + field::kRetrofit);
    if (filter)
        ReleaseOf(filter);
    if (tool)
        ReleaseOf(tool);
}

// ====================================================================================
// simulators.netmgr.nodes: CreateConnectionNode, AutoconnectLotNodes
// ====================================================================================

// Slot 20 (0x00611300, ret 0x1C: the address of an occupant pointer, a
// network type, the address of a cell (x, z), two words of edge types, a
// path ID override or 0, and a rotation byte; gzcom-dll's header has it at
// slot 21). A connection node is the network piece a lot brings with it: the
// cell of the lot where a network may join it (the lot's configuration has
// it as an object of type 7 with the network type, the two words and
// optionally the path ID; the lot developer's 0x006C9C80 calls this for each
// and AutoconnectLotNodes with all of them).
//
// There is no refusing path before the city is touched. First the manager's
// kept tool of the type (its own slot 5, second argument 0; not tested for
// null, so a type of 13 or more faults as in the game): Init (slot 3), slot
// 8 with the cell, 0x100, 0 and the type, which places the piece, Shutdown
// (slot 4). Then a network filter of the type's flag is made and
// referenced, and the first occupant it takes on the cell is asked of the
// occupant manager (0x00B43D0C, slot 32). With none, or with one that does
// not answer the network occupant interface: AL 0, the caller's pointer
// untouched.
//
// The piece is then made a node:
//   - when the lot manager (0x00B43D08, not tested for null) has a lot on
//     the cell (slot 6: x, z, 0) and the lot a building (slot 48): the
//     building's occupant part (slot 3, not tested) gives its position (slot
//     8: three floats; the game's three words are not set before), and the
//     position's second float, the height, goes through the type's tool
//     (slot 5 again, slot 74: the general tool gives the float back) into
//     all four corners of the piece (slot 36);
//   - with a path ID override: slot 16 stores it, and slot 30 sets the
//     piece's own ID (slot 23) again with the rotation byte;
//   - the flags 0x200000 and 0x8000000 are set and 0x2000000 and 0x4000000
//     cleared; the piece is made unusable (slot 20 with 0); the two words of
//     edge types are stored (slot 14, in the arguments' order);
//   - the caller's pointer gets the occupant (what was there is not
//     released) and the occupant a reference for it; AL 1.
// Last the piece, the filter and the occupant's own reference are released,
// each when not null.
//
// The game keeps the answer's byte in its type argument's place and the
// piece in its cell argument's (the cell's address is in EBP by then). The
// cell is read through its address each time it is used, as here.
uint8_t __fastcall CreateConnectionNode(char* self, void*, void** out, uint32_t type, const uint32_t* cell,
                                        uint32_t firstTypes, uint32_t secondTypes, uint32_t pathId,
                                        uint32_t rotation)
{
    static bool logged = false;
    FirstCall("CreateConnectionNode", logged);
    void* const tool = Virtual<void*>(self, slot::kGetNetworkTool, type, 0u);
    Virtual<void>(tool, tool_slot::kInit);
    Virtual<void>(tool, tool_slot::kSlot8, cell[0], cell[1], kNodeToolFlag, 0u, type);
    Virtual<void>(tool, tool_slot::kShutdown);
    void* const filter = MakeMaskFilter(TypeFlag(type));
    void* const occupants = Occupants();
    void* found = nullptr;
    uint8_t made = 0;
    if (Virtual<uint8_t>(occupants, occupants_slot::kGetFirstByCityCell, &found, cell[0], cell[1], filter)) {
        void* piece = nullptr;
        if (Virtual<uint8_t>(found, 0x0, kIidNetworkOccupant, &piece)) {
            void* const lots = Global<void*>(addr::SC4_LotManager);
            void* const lot = Virtual<void*>(lots, lots_slot::kGetLot, cell[0], cell[1], 0u);
            if (lot) {
                if (void* const building = Virtual<void*>(lot, lot_slot::kGetBuilding)) {
                    void* const part = Virtual<void*>(building, building_slot::kAsOccupant);
                    uint32_t position[3] = {0u, 0u, 0u};
                    Virtual<void>(part, occupant_slot::kGetPosition, &position[0]);
                    const uint32_t height = position[1];
                    void* const again = Virtual<void*>(self, slot::kGetNetworkTool, type, 0u);
                    const float level = static_cast<float>(Virtual<double>(again, tool_slot::kSlot74, height));
                    float heights[4] = {level, level, level, level};
                    Virtual<void>(piece, piece_slot::kSetVertexHeights, &heights[0]);
                }
            }
            if (pathId) {
                Virtual<void>(piece, piece_slot::kSetConnectionPathIDOverride, pathId);
                const uint32_t id = Virtual<uint32_t>(piece, piece_slot::kPieceId);
                Virtual<void>(piece, piece_slot::kSetPieceAndRotation, id, rotation);
            }
            Virtual<void>(piece, piece_slot::kSetNetworkFlag, kFlagNode);
            Virtual<void>(piece, piece_slot::kSetNetworkFlag, kFlagRetrofitted);
            Virtual<void>(piece, piece_slot::kClearNetworkFlag, kFlagNew1);
            Virtual<void>(piece, piece_slot::kClearNetworkFlag, kFlagNew2);
            Virtual<void>(piece, piece_slot::kSetUsable, 0u);
            Virtual<void>(piece, piece_slot::kSetConnectionNodeEdgeTypes, firstTypes, secondTypes);
            *out = found;
            AddRefOf(found);
            made = 1;
        }
        if (piece)
            ReleaseOf(piece);
    }
    if (filter)
        ReleaseOf(filter);
    if (found)
        ReleaseOf(found);
    return made;
}

// Whether a cell lies in a rectangle of cells (low x, low z, high x, high
// z), the bounds included; signed compares, in the game's order.
bool Inside(const int32_t* rect, int32_t x, int32_t z)
{
    return x >= rect[0] && z >= rect[1] && x <= rect[2] && z <= rect[3];
}

// Slot 21 (0x00611530, ret 8: a lot and the address of a list of occupants,
// the lot's nodes as CreateConnectionNode gave them; gzcom-dll's header has
// it at slot 22): every node is joined to the network that lies beside it.
//
// Two network filters are made and referenced, of the masks 0x1F4F (the
// surface networks) and 0xB0 (pipe, power line, subway); the lot's rectangle
// of cells is asked (slot 14). For an empty list that is all. Else for every
// node of the list (the head node read once):
//   - it is asked for the network occupant interface (the answer is not
//     looked at and the piece used without a test);
//   - its network type is the first of the thirteen whose flag it has (slot
//     8); a piece with none counts as type 13, whose "flag" is the word
//     behind the table (see TypeFlag) and whose tool is null, as in the
//     game;
//   - the filter is the first when the type's flag is one of 0x1F4F, else
//     the second;
//   - the piece's cell (slot 59) and its two words of edge types (slot 13);
//     of the second word a bit for each of the four edges whose byte has one
//     of its low three bits set; less the edges the piece has active already
//     (slot 40) those are the edges to join;
//   - for each such edge, in the order of the neighbour tables at 0x00AA7A18
//     and 0x00AA7A28 (x - 1, z - 1, x + 1, z + 1): the neighbour cell. For
//     the last two edges a neighbour inside the lot's rectangle is skipped
//     (the first two are not tested). The first occupant the filter takes
//     there is asked of the occupant manager (slot 32); with none, nothing.
//     With one: while the cell is inside the lot's rectangle the next cell
//     the same way is tried (the occupant found before is released first);
//     when it has an occupant too the walk goes on from there, else it
//     stops at the cell before. Then the type's kept tool (the manager's own
//     slot 5; referenced when not null and then used without a test): Init,
//     slot 7 from the node's cell to the cell the walk reached (x, z, to x,
//     to z, 0, 0, 1, -1), Shutdown, Release. The occupant found last is
//     released;
//   - the piece is released.
// Last the second filter is released, then the first.
//
// The function tests no cell against the city's size: it relies on the
// occupant manager's lookup. The game keeps the piece in its lot argument's
// place and the occupant in its list argument's. The two words of edge types
// are not set before slot 13, which leaves them alone for a piece without
// the property: the game then reads its stack, ours zero (no edge to join).
// The game's EAX is what the last call left: nothing is returned.
void __fastcall AutoconnectLotNodes(char* self, void*, void* lot, ListNode** nodes)
{
    static bool logged = false;
    FirstCall("AutoconnectLotNodes", logged);
    void* const surface = MakeMaskFilter(kSurfaceMask);
    void* const others = MakeMaskFilter(kOtherNetworksMask);
    void* const occupants = Occupants();
    int32_t rect[4] = {0, 0, 0, 0};
    Virtual<void>(lot, lot_slot::kGetBoundingRect, &rect[0]);
    ListNode* const head = *nodes;
    for (ListNode* node = head->next; node != head; node = node->next) {
        void* piece = nullptr;
        Virtual<uint8_t>(node->value, 0x0, kIidNetworkOccupant, &piece);
        uint32_t type = 0;
        for (; type < kTypes; ++type)
            if (Virtual<uint8_t>(piece, piece_slot::kHasNetworkFlag, TypeFlag(type)))
                break;
        void* const filter = (TypeFlag(type) & kSurfaceMask) != 0 ? surface : others;
        uint32_t x = 0, z = 0;
        Virtual<void>(piece, piece_slot::kGetOccupiedCell, &x, &z);
        uint32_t firstTypes = 0, secondTypes = 0;
        Virtual<uint8_t>(piece, piece_slot::kGetConnectionNodeEdgeTypes, &firstTypes, &secondTypes);
        const uint32_t edges = ((((secondTypes & kEdgeLowBits) + kEdgeLowBits) & kEdgeHighBits) * kEdgeGather) >> 27;
        const uint32_t wanted = ~static_cast<uint32_t>(Virtual<uint8_t>(piece, piece_slot::kGetActiveEdges)) & edges;
        for (uint32_t direction = 0; direction < 4; ++direction) {
            if (!(wanted & (1u << direction)))
                continue;
            const uint32_t stepX = Global<uint32_t>(addr::cSC4NetworkManager_NeighborDx + 4 * direction);
            const uint32_t stepZ = Global<uint32_t>(addr::cSC4NetworkManager_NeighborDz + 4 * direction);
            uint32_t toX = stepX + x, toZ = stepZ + z;
            if (direction >= 2 && Inside(rect, static_cast<int32_t>(toX), static_cast<int32_t>(toZ)))
                continue;
            void* found = nullptr;
            if (Virtual<uint8_t>(occupants, occupants_slot::kGetFirstByCityCell, &found, toX, toZ, filter)) {
                while (Inside(rect, static_cast<int32_t>(toX), static_cast<int32_t>(toZ))) {
                    const uint32_t nextX = stepX + toX, nextZ = stepZ + toZ;
                    if (found) {
                        void* const before = found;
                        found = nullptr;
                        ReleaseOf(before);
                    }
                    if (!Virtual<uint8_t>(occupants, occupants_slot::kGetFirstByCityCell, &found, nextX, nextZ, filter))
                        break;
                    toX = nextX;
                    toZ = nextZ;
                }
                void* const tool = Virtual<void*>(self, slot::kGetNetworkTool, type, 0u);
                if (tool)
                    AddRefOf(tool);
                Virtual<void>(tool, tool_slot::kInit);
                Virtual<void>(tool, tool_slot::kSlot7, x, z, toX, toZ, 0u, 0u, 1u, 0xFFFFFFFFu);
                Virtual<void>(tool, tool_slot::kShutdown);
                ReleaseOf(tool);
            }
            if (found)
                ReleaseOf(found);
        }
        if (piece)
            ReleaseOf(piece);
    }
    if (others)
        ReleaseOf(others);
    if (surface)
        ReleaseOf(surface);
}

// ====================================================================================
// Installation
// ====================================================================================

const Detour kRetrofitDetours[] = {
    OPENSC4_NETMGR_DETOUR(addr::cSC4NetworkManager_ProcessRetrofitList, ProcessRetrofitList),
};

const Detour kNodeDetours[] = {
    OPENSC4_NETMGR_DETOUR(addr::cSC4NetworkManager_CreateConnectionNode, CreateConnectionNode),
    OPENSC4_NETMGR_DETOUR(addr::cSC4NetworkManager_AutoconnectLotNodes, AutoconnectLotNodes),
};

bool InstallRetrofit(registry::Mode) { return InstallDetours(kRetrofitDetours, "retrofit"); }
bool InstallNodes(registry::Mode) { return InstallDetours(kNodeDetours, "nodes"); }

const bool g_registered[] = {
    registry::Register({"simulators.netmgr.retrofit", "simulators/netmgr", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4NetworkManager_ProcessRetrofitList,
                        "Our network manager retrofit pass: ProcessRetrofitList, which gives the pieces of an old "
                        "save's interchanges their piece IDs (1 detour; src/core/netmgr_nodes.cpp)",
                        false, InstallRetrofit}),
    registry::Register({"simulators.netmgr.nodes", "simulators/netmgr", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4NetworkManager_CreateConnectionNode,
                        "Our network manager lot nodes: CreateConnectionNode and AutoconnectLotNodes (2 detours; "
                        "src/core/netmgr_nodes.cpp)",
                        false, InstallNodes}),
};

} // namespace
} // namespace opensc4::netmgr_sim
