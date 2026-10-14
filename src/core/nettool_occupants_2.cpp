// Our cSC4NetworkTool, part t7, second file (build 641; the part's analysis
// (NAM, registers, wrappers, floats, stack leftovers, the call list) is at
// the top of src/core/nettool_occupants_1.cpp, the layouts in
// nettool_occupants.h): the RUL0 intersections' occupants, the tunnels, the
// city-edge connections, slot 20 GetCostOfSolution and the major bridges'
// cell vectors. Four groups:
//   simulators.nettool.ruleoccupants: PlaceRuleOccupants 0x0062C3A0,
//     PlaceRuleCellOccupant 0x006276D0, the rule's cons cell lookup
//     0x00624F00 and its table's find 0x00624D20 (4 detours);
//   simulators.nettool.tunnels: PlaceTunnels 0x006287D0, MakeTunnelPiece
//     0x00628390, AddEdgeConnection 0x00628BD0 (3 detours);
//   simulators.nettool.cost: slot 20 GetCostOfSolution 0x00625640 (1);
//   simulators.nettool.bridgecells: Assign 0x00626750, CopyRange
//     0x006272A0, AllocateCopy 0x0062A420, DestroyRange 0x0062BEF0, the
//     vector's operator= 0x0062C1E0 (5 detours).
//
// Registers (this file's functions; slot 20 at the first file's top). The
// game callers after each call: PlaceRuleOccupants 0x0062C4EE (nothing
// read), PlaceRuleCellOccupant 0x0062C46F (nothing), the cons cell lookup
// 0x0062C42E (EAX), the table find 0x00624F43 (the out only), PlaceTunnels
// 0x0063BCBB (nothing), MakeTunnelPiece 0x006289A5 and 0x00628A81 (EAX),
// AddEdgeConnection 0x0063BBD5 and 0x00652A34 (AL), Assign 0x006272CB
// (nothing), CopyRange 0x0062C27C / 0x0062C29A (EAX), AllocateCopy
// 0x0062C234 (EAX), DestroyRange 0x0062C286 (nothing), the operator=
// 0x0062C567 (nothing). None reads ECX or EDX: plain detours, each
// answering what the game's answers where it is read (the operator= EAX
// this, Assign EAX this, CopyRange the destination's end, the table find
// the out).
#include "core/nettool_occupants.h"

#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace opensc4::nettool_sim {
namespace {

using namespace occupants;

// ====================================================================================
// The RUL0 intersections' occupants
// ====================================================================================

// 0x00624D20 (ret 8: an iterator out, the key's address; this a hash table
// keyed by a byte, the RUL0 rule's cons cells +0xD0): the node of the key
// (bucket key % ((end - begin) >> 2), then the chain) or null, and the
// table, into the out; EAX the out.
uint32_t* __fastcall ConsTableFind(char* table, void*, uint32_t* out, const uint8_t* key)
{
    static bool logged = false;
    FirstCall("ConsTableFind", logged);
    const uint8_t wanted = *key;
    char* const begin = At<char*>(table, rul::table::kBuckets);
    const uint32_t count =
        static_cast<uint32_t>(static_cast<int32_t>(At<char*>(table, rul::table::kBucketsEnd) - begin) >> 2);
    char* node =
        reinterpret_cast<char**>(At<char*>(table, rul::table::kBuckets))[static_cast<uint32_t>(wanted) % count];
    while (node && At<uint8_t>(node, rul::table::kKey) != wanted)
        node = At<char*>(node, rul::table::kNext);
    out[0] = static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(node));
    out[1] = static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(table));
    return out;
}

// 0x00624F00 (ret 8: x, z relative to the rule; this a RUL0 rule; called by
// PlaceRuleOccupants): the cons cell (node + 8) of the cell grid's value at
// the point, or 0 when the point is outside the grid (column >= width or
// index >= size, both unsigned) or the value has none.
char* __fastcall ConsCellAt(char* rule, void*, uint32_t x, uint32_t z)
{
    static bool logged = false;
    FirstCall("ConsCellAt", logged);
    const uint32_t column = x - At<uint32_t>(rule, rul::rul0::kMinX);
    const uint32_t width = At<uint32_t>(rule, rul::rul0::kCellWidth);
    if (column >= width)
        return nullptr;
    const uint32_t index = (z - At<uint32_t>(rule, rul::rul0::kMinZ)) * width + column;
    char* const grid = At<char*>(rule, rul::rul0::kCellGrid);
    if (index >= static_cast<uint32_t>(At<char*>(rule, rul::rul0::kCellGrid + 4) - grid))
        return nullptr;
    uint32_t iterator[2] = {0, 0};
    CallThis<uint32_t*, uint32_t*, const uint8_t*>(addr::SC4Rul0ConsTable_Find, rule + rul::rul0::kConsCells, iterator,
                                                   reinterpret_cast<const uint8_t*>(grid + index));
    char* const node = reinterpret_cast<char*>(static_cast<std::uintptr_t>(iterator[0]));
    return node ? node + rul::store::kRule : nullptr;
}

// 0x006276D0 (ret 0x20: a cell info, a list or null, a flag byte, a piece
// instance, a rotation, the complex piece's origin x and z, a path ID;
// called by PlaceRuleOccupants): a prebuilt occupant (slot 54 with
// 0x49C1A034), its position asked (the occupant part's GetPosition; not
// used after), its SetUp with the RUL0 group 0xA8434037 and a zero offset
// (SetUpPrebuiltOccupant), the piece and rotation (slot 0x78), flags (0x2000000
// and 0x4000000 cleared, 0x8000000 set, 0x1000000 when (h3 + h1) is below
// (h2 + h0)), UpdatePlanes, the path ID override when not 0, the cell
// cleared (ClearCellForOccupant with the tool's filter), the occupant into
// the occupant manager (the cell's +0x54 its answer), with the flag byte
// 0x10000000 set and 0x4000 cleared, 0x500000 set, the complex piece origin,
// appended to the list (referenced through its occupant part) and
// released.
void __fastcall PlaceRuleCellOccupant(char* self, void*, char* info, ListNode** list, uint32_t flag,
                                      uint32_t instance, uint32_t rotation, uint32_t originX, uint32_t originZ,
                                      uint32_t pathId)
{
    static bool logged = false;
    FirstCall("PlaceRuleCellOccupant", logged);
    void* occupant = nullptr;
    uint32_t box[12] = {};
    Virtual<void>(self, tslot::kCreateNetworkOccupant, info, &occupant, box, kKindPrebuilt);
    uint32_t position[3] = {0, 0, 0};
    Virtual<void>(Virtual<void*>(occupant, oslot::kAsOccupant), oslot::kGetPosition, position);
    const uint32_t offset[3] = {0, 0, 0};
    CallThis<void, void*, uint32_t, const uint32_t*, uint32_t, uint32_t>(
        addr::cSC4NetworkTool_SetUpPrebuiltOccupant, self, occupant, instance, offset, rotation, kRuleCellGroup);
    Virtual<void>(occupant, oslot::kSetPiece, instance, rotation);
    ClearFlag(occupant, oflag::kFlag2000000);
    ClearFlag(occupant, oflag::kFlag4000000);
    SetFlag(occupant, oflag::kFlag8000000);
    const double diagonal02 = static_cast<double>(At<float>(info, cell::kHeights + 8)) +
                              static_cast<double>(At<float>(info, cell::kHeights));
    const double diagonal13 = static_cast<double>(At<float>(info, cell::kHeights + 12)) +
                              static_cast<double>(At<float>(info, cell::kHeights + 4));
    // `fcompp; test ah, 5; jp`: only when below (ordered).
    if (diagonal13 < diagonal02)
        SetFlag(occupant, oflag::kPlanar);
    CallThis<void>(addr::cSC4PrebuiltNetworkOccupant_UpdatePlanes, occupant);
    if (pathId)
        Virtual<void>(occupant, oslot::kSetPathIdOverride, pathId);
    CallThis<void, uint32_t, uint32_t, void*>(addr::cSC4NetworkTool_ClearCellForOccupant, self,
                                              At<uint32_t>(info, cell::kX), At<uint32_t>(info, cell::kZ),
                                              At<void*>(self, field::kFilter));
    if (void* const manager = At<void*>(self, field::kOccupants)) {
        const uint32_t word = At<uint32_t>(self, field::kWord208);
        void* const asOccupant = Virtual<void*>(occupant, oslot::kAsOccupant);
        At<uint8_t>(info, cell::kByte54) = Virtual<uint8_t>(manager, xslot::kInsertOccupant, asOccupant, word);
    }
    if (static_cast<uint8_t>(flag)) {
        SetFlag(occupant, oflag::kUsable);
        ClearFlag(occupant, oflag::kTranslucent);
    }
    SetFlag(occupant, oflag::kFlags500000);
    Virtual<void>(occupant, oslot::kSetComplexOrigin, originX, originZ);
    if (list) {
        AddRefOf(Virtual<void*>(occupant, oslot::kAsOccupant));
        PushBack(*list, occupant);
    }
    if (occupant)
        ReleaseOf(occupant);
}

// 0x0062C3A0 (ret 0x18: x, z, a RUL0 rule ID, a list or null, a placing
// byte, a flag byte; called by 0x0062C4B0): the rule from the store
// (FindOrInsert: an absent ID gets an empty rule, as in the game); the
// tool's +0x50 saved and set to the placing byte; when it is set, for each
// checked point of the rule with a cell info at (x, z) + the point and a
// cons cell there (0x00624F00): PlaceRuleCellOccupant with the piece
// (rule +0xC8 + the cons cell's ID), the cons cell's turn, (x, z) as the
// origin and the path ID (rule +0xCC + the cons cell's ID, or 0 when +0xCC
// is 0). +0x50 put back.
void __fastcall PlaceRuleOccupants(char* self, void*, uint32_t x, uint32_t z, uint32_t ruleId, ListNode** list,
                                   uint32_t placing, uint32_t flag)
{
    static bool logged = false;
    FirstCall("PlaceRuleOccupants", logged);
    char* const rule = CallThis<char*, uint32_t*>(
        addr::SC4Rul0Store_FindOrInsert, reinterpret_cast<void*>(addr::cSC4NetworkTool_Rul0Store), &ruleId);
    const uint8_t saved = At<uint8_t>(self, field::kReplacing);
    At<uint8_t>(self, field::kReplacing) = static_cast<uint8_t>(placing);
    if (static_cast<uint8_t>(placing)) {
        char* const end = At<Vector>(rule, rul::rul0::kChecked).end;
        for (char* point = At<Vector>(rule, rul::rul0::kChecked).begin; point != end; point += rul::point::kSize) {
            const uint32_t cellZ = At<uint32_t>(point, rul::point::kZ) + z;
            const uint32_t cellX = At<uint32_t>(point, rul::point::kX) + x;
            char* const info = Entry(self, (cellZ << 16) + cellX);
            if (!info)
                continue;
            char* const value =
                CallThis<char*, uint32_t, uint32_t>(addr::SC4Rul0Rule_ConsCellAt, rule, cellX - x, cellZ - z);
            if (!value)
                continue;
            const uint32_t pathBase = At<uint32_t>(rule, orule::kPathBase);
            const uint32_t pathId = pathBase ? At<uint32_t>(value, orule::kValueId) + pathBase : 0u;
            CallThis<void, char*, ListNode**, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t>(
                addr::cSC4NetworkTool_PlaceRuleCellOccupant, self, info, list, flag,
                At<uint32_t>(rule, orule::kPieceBase) + At<uint32_t>(value, orule::kValueId),
                static_cast<uint32_t>(At<uint8_t>(value, orule::kValueTurn)), x, z, pathId);
        }
    }
    At<uint8_t>(self, field::kReplacing) = saved;
}

// ====================================================================================
// The tunnels
// ====================================================================================

// The smaller and the larger of two words as the game picks them (signed
// compares).
inline uint32_t Smaller(uint32_t a, uint32_t b) { return static_cast<int32_t>(a) < static_cast<int32_t>(b) ? a : b; }
inline uint32_t Larger(uint32_t a, uint32_t b) { return static_cast<int32_t>(b) < static_cast<int32_t>(a) ? a : b; }

// 0x00628390 (ret 0xC: a direction (its low byte and low two bits read), an
// index (its low byte), a cell info; called by PlaceTunnels): 0 when the
// parameters have no tunnel pieces (+0x2E4). Else a tunnel occupant (slot
// 54 with 0x8A4BD52B), flags (0x2000000, 0x4000000, 0x4000 cleared,
// 0x10000000 set), its SetUp (the index's piece +0x2E4, turn (+0x2F0's low
// byte + the direction) & 3, a zero offset, the row's group), the cell
// cleared, the occupant into the occupant manager; the two corners on the
// entrance side ((-1 - dir) & 3 and the next) raised by the index's float
// (+0x2FC), all four corners' vertices set to the cell's heights in the
// terrain, the two vertices in line beyond the entrance raised to the
// corners' heights where the terrain is below them, and the rectangle
// around the changed vertices redrawn (terrain slots 0x15C, 0x110 (1, 1,
// the rectangle, 6), 0x160). The occupant is released; EAX it.
void* __fastcall MakeTunnelPiece(char* self, void*, uint32_t direction, uint32_t index, char* info)
{
    static bool logged = false;
    FirstCall("MakeTunnelPiece", logged);
    void* occupant = nullptr;
    if (At<char*>(self, ofield::kTunnelPieces) == At<char*>(self, ofield::kTunnelPiecesEnd))
        return nullptr;
    uint32_t box[12] = {};
    Virtual<void>(self, tslot::kCreateNetworkOccupant, info, &occupant, box, kKindTunnel);
    ClearFlag(occupant, oflag::kFlag2000000);
    ClearFlag(occupant, oflag::kFlag4000000);
    ClearFlag(occupant, oflag::kTranslucent);
    SetFlag(occupant, oflag::kUsable);
    const uint32_t slot = static_cast<uint32_t>(static_cast<uint8_t>(index)) * 4;
    const uint32_t offset[3] = {0, 0, 0};
    const uint32_t turn = static_cast<uint8_t>(At<uint8_t>(At<char*>(self, ofield::kTunnelTurns), slot) +
                                               static_cast<uint8_t>(direction)) &
                          3u;
    CallThis<void, void*, uint32_t, const uint32_t*, uint32_t, uint32_t>(
        addr::cSC4NetworkTool_SetUpPrebuiltOccupant, self, occupant,
        At<uint32_t>(At<char*>(self, ofield::kTunnelPieces), slot), offset, turn, 0u);
    CallThis<void, uint32_t, uint32_t, void*>(addr::cSC4NetworkTool_ClearCellForOccupant, self,
                                              At<uint32_t>(info, cell::kX), At<uint32_t>(info, cell::kZ),
                                              At<void*>(self, field::kFilter));
    if (void* const manager = At<void*>(self, field::kOccupants)) {
        const uint32_t word = At<uint32_t>(self, field::kWord208);
        void* const asOccupant = Virtual<void*>(occupant, oslot::kAsOccupant);
        At<uint8_t>(info, cell::kByte54) = Virtual<uint8_t>(manager, xslot::kInsertOccupant, asOccupant, word);
    }
    const double raise = static_cast<double>(At<float>(At<char*>(self, ofield::kTunnelHeights), slot));
    const uint32_t c0 = (0xFFFFFFFFu - direction) & 3u;
    const uint32_t c1 = (c0 + 1) & 3u;
    At<float>(info, cell::kHeights + 4 * c0) =
        static_cast<float>(raise + static_cast<double>(At<float>(info, cell::kHeights + 4 * c0)));
    At<float>(info, cell::kHeights + 4 * c1) =
        static_cast<float>(raise + static_cast<double>(At<float>(info, cell::kHeights + 4 * c1)));
    const uint32_t perRow = At<uint32_t>(self, field::kVertexStride);
    const uint32_t perColumn = At<uint32_t>(self, ofield::kVerticesZ);
    const uint32_t v1 = At<uint32_t>(info, cell::kVertices + 4 * c1);
    const uint32_t v0 = At<uint32_t>(info, cell::kVertices + 4 * c0);
    uint32_t minX = Smaller(v1 % perRow, v0 % perRow);
    uint32_t maxX = Larger(v1 % perRow, v0 % perRow);
    uint32_t minZ = Smaller(v1 / perColumn, v0 / perColumn);
    uint32_t maxZ = Larger(v1 / perColumn, v0 / perColumn);
    for (std::size_t k = 0; k < 4; ++k)
        Virtual<void>(Terrain(self), xslot::kTerrainSetVertexAltitude, At<uint32_t>(info, cell::kVertices + 4 * k),
                      At<uint32_t>(info, cell::kHeights + 4 * k));
    const uint32_t beyond0 =
        At<uint32_t>(info, cell::kVertices + 4 * c0) * 2 - At<uint32_t>(info, cell::kVertices + 4 * ((c0 - 1) & 3u));
    const uint32_t beyond1 =
        At<uint32_t>(info, cell::kVertices + 4 * c1) * 2 - At<uint32_t>(info, cell::kVertices + 4 * ((c1 + 1) & 3u));
    const uint32_t beyond[2] = {beyond0, beyond1};
    const uint32_t corner[2] = {c0, c1};
    for (std::size_t i = 0; i < 2; ++i) {
        const double terrain = Virtual<double>(Terrain(self), xslot::kTerrainVertexAltitude, beyond[i]);
        // `fcomp; test ah, 5; jp`: only when below (ordered).
        if (!(terrain < static_cast<double>(At<float>(info, cell::kHeights + 4 * corner[i]))))
            continue;
        Virtual<void>(Terrain(self), xslot::kTerrainSetVertexAltitude, beyond[i],
                      At<uint32_t>(info, cell::kHeights + 4 * corner[i]));
        const uint32_t x = beyond[i] % At<uint32_t>(self, field::kVertexStride);
        minX = Smaller(x, minX);
        maxX = static_cast<int32_t>(maxX) < static_cast<int32_t>(x) ? x : maxX;
        const uint32_t z = beyond[i] / At<uint32_t>(self, ofield::kVerticesZ);
        minZ = Smaller(z, minZ);
        maxZ = static_cast<int32_t>(maxZ) < static_cast<int32_t>(z) ? z : maxZ;
    }
    uint32_t rect[4] = {minX, minZ, maxX, maxZ};
    Virtual<void>(Terrain(self), xslot::kTerrainBeginUpdate);
    Virtual<void>(Terrain(self), xslot::kTerrainUpdateRect, 1u, 1u, rect, 6u);
    Virtual<void>(Terrain(self), xslot::kTerrainEndUpdate);
    if (occupant)
        ReleaseOf(occupant);
    return occupant;
}

// 0x006287D0 (ret 4: a network type, for the preview height; called by the
// segment placer 0x0063B830): for each +0xE4 record (the first and the last
// step of a tunnel) while its last step is below the valid step count (the
// first that is not ends the function). Previewing (+0x50 clear): every
// cell of its steps erased in the preview drawable and added green
// (0x8000FF00, no heights, the type's preview height). Placing: the two
// entrances' first cells must share a row or a column (else the function
// ends); the direction (0 to 3) and whether the walk is reversed from them;
// at the first entrance each cell of its step (in the walk's order) marked
// (+0x51) and given a tunnel piece (MakeTunnelPiece with the direction and
// a count), the pieces kept in a list; at the last entrance each cell
// marked and given a piece with the opposite direction, each paired with
// the last piece of the list (taken off): both entrances answer the tunnel
// interface, each gets the other as its other end, and each one's path
// information (slot 0xC4) links the two (its slot 0x80). The list freed.
void __fastcall PlaceTunnels(char* self, void*, uint32_t type)
{
    static bool logged = false;
    FirstCall("PlaceTunnels", logged);
    char* const recordsEnd = At<char*>(self, ofield::kRecordsEnd);
    for (char* record = At<char*>(self, field::kVectorE4); record != recordsEnd; record += orecord::kSize) {
        if (At<int32_t>(record, orecord::kLast) >= At<int32_t>(self, field::kValidSteps))
            return;
        if (!At<uint8_t>(self, field::kReplacing)) {
            if (At<int32_t>(record, orecord::kFirst) > At<int32_t>(record, orecord::kLast))
                continue;
            for (int32_t s = At<int32_t>(record, orecord::kFirst);; ++s) {
                uint32_t c = At<uint32_t>(Steps(self) + static_cast<uint32_t>(s) * step::kSize, 0);
                if (c <= At<uint32_t>(Steps(self) + static_cast<uint32_t>(s) * step::kSize, step::kLastCell)) {
                    do {
                        const int32_t xz[2] = {At<int32_t>(Cells(self) + c * kPointSize, 0),
                                               At<int32_t>(Cells(self) + c * kPointSize, 4)};
                        CallThis<void, const int32_t*>(addr::cSC4NetworkToolPreview_EraseCell,
                                                       At<char*>(self, field::kPreview), xz);
                        CallThis<void, const int32_t*, uint32_t, char*, uint32_t, uint32_t>(
                            addr::cSC4NetworkToolPreview_AddCell, At<char*>(self, field::kPreview), xz, kColourTunnel,
                            nullptr, PreviewHeight(type), 0u);
                        ++c;
                    } while (c <= At<uint32_t>(Steps(self) + static_cast<uint32_t>(s) * step::kSize, step::kLastCell));
                }
                if (s + 1 > At<int32_t>(record, orecord::kLast))
                    break;
            }
            continue;
        }
        const uint32_t firstStep = At<uint32_t>(record, orecord::kFirst);
        const uint32_t lastStep = At<uint32_t>(record, orecord::kLast);
        const uint32_t a = At<uint32_t>(Steps(self) + firstStep * step::kSize, 0);
        const uint32_t b = At<uint32_t>(Steps(self) + lastStep * step::kSize, 0);
        const int32_t ax = At<int32_t>(Cells(self) + a * kPointSize, 0);
        const int32_t az = At<int32_t>(Cells(self) + a * kPointSize, 4);
        const int32_t bx = At<int32_t>(Cells(self) + b * kPointSize, 0);
        const int32_t bz = At<int32_t>(Cells(self) + b * kPointSize, 4);
        // The game's direction is the low byte of a frame dword (see the
        // first file's top); ours is 0 above it.
        uint32_t direction;
        bool reversed;
        if (az == bz) {
            direction = bx <= ax ? 3u : 1u;
            reversed = bx < ax;
        } else if (ax == bx) {
            direction = bz <= az ? 0u : 2u;
            reversed = bz > az;
        } else {
            return;
        }
        ListNode* const head = CallCdecl<ListNode*, uint32_t>(addr::SC4_stl_allocate, sizeof(ListNode));
        head->next = head;
        head->previous = head;
        // The first entrance.
        uint32_t from = At<uint32_t>(Steps(self) + At<uint32_t>(record, orecord::kFirst) * step::kSize, 0);
        uint32_t to = At<uint32_t>(Steps(self) + At<uint32_t>(record, orecord::kFirst) * step::kSize, step::kLastCell);
        uint32_t made = 0;
        uint32_t stride = 1;
        if (reversed && static_cast<int32_t>(from) < static_cast<int32_t>(to)) {
            const uint32_t swap = from;
            from = to;
            to = swap;
            stride = 0xFFFFFFFFu;
        }
        to += stride;
        for (uint32_t c = from; c != to; c += stride) {
            char* const info = Entry(self, PointKey(Cells(self) + c * kPointSize));
            At<uint8_t>(info, cell::kOccupied) = 1;
            if (At<uint8_t>(self, field::kReplacing)) {
                void* const piece = CallThis<void*, uint32_t, uint32_t, char*>(addr::cSC4NetworkTool_MakeTunnelPiece,
                                                                               self, direction, made, info);
                ++made;
                PushBack(head, piece);
            }
        }
        // The last entrance.
        from = At<uint32_t>(Steps(self) + At<uint32_t>(record, orecord::kLast) * step::kSize, 0);
        to = At<uint32_t>(Steps(self) + At<uint32_t>(record, orecord::kLast) * step::kSize, step::kLastCell);
        uint32_t madeLast = 0;
        stride = 1;
        if (!reversed && static_cast<int32_t>(from) < static_cast<int32_t>(to)) {
            const uint32_t swap = from;
            from = to;
            to = swap;
            stride = 0xFFFFFFFFu;
        }
        to += stride;
        for (uint32_t c = from; c != to; c += stride) {
            char* const info = Entry(self, PointKey(Cells(self) + c * kPointSize));
            At<uint8_t>(info, cell::kOccupied) = 1;
            if (!At<uint8_t>(self, field::kReplacing))
                continue;
            const uint32_t opposite = static_cast<uint8_t>(static_cast<uint8_t>(direction) - 2) & 3u;
            void* const piece = CallThis<void*, uint32_t, uint32_t, char*>(addr::cSC4NetworkTool_MakeTunnelPiece, self,
                                                                           opposite, madeLast, info);
            ListNode* const node = head->previous;
            ListNode* const next = node->next;
            ListNode* const previous = node->previous;
            ++madeLast;
            void* const other = node->value;
            previous->next = next;
            next->previous = previous;
            CallCdecl<void, void*>(addr::SC4_Free, node);
            void* near = nullptr;
            void* far = nullptr;
            if (Virtual<uint8_t>(other, oslot::kQueryInterface, kKindTunnel, &near)) {
                if (far) {
                    void* const old = far;
                    far = nullptr;
                    ReleaseOf(old);
                }
                if (Virtual<uint8_t>(piece, oslot::kQueryInterface, kKindTunnel, &far)) {
                    CallThis<void, void*>(addr::cSC4NetworkTunnelOccupant_SetOtherEnd, near, piece);
                    CallThis<void, void*>(addr::cSC4NetworkTunnelOccupant_SetOtherEnd, far, other);
                    if (void* const path = Virtual<void*>(near, oslot::kGetPathInfo))
                        Virtual<void>(path, oslot::kPathLink, near, far);
                    if (void* const path = Virtual<void*>(far, oslot::kGetPathInfo))
                        Virtual<void>(path, oslot::kPathLink, far, near);
                }
            }
            if (far)
                ReleaseOf(far);
            if (near)
                ReleaseOf(near);
        }
        for (ListNode* node = head->next; node != head;) {
            ListNode* const gone = node;
            node = node->next;
            CallCdecl<void, void*>(addr::SC4_Free, gone);
        }
        head->next = head;
        head->previous = head;
        CallCdecl<void, void*>(addr::SC4_Free, head);
    }
}

// ====================================================================================
// The city-edge connections
// ====================================================================================

// The neighbours simulator's (0x00B43D8C) slot 0x30 (x, z): what it has at
// a cell beyond the city's edge.
inline uint32_t NeighborAt(void* neighbors, uint32_t x, uint32_t z)
{
    return Virtual<uint32_t>(neighbors, xslot::kNeighborCityAt, x, z);
}

// 0x00628BD0 (ret 8: a word for DrawNetworkLine, a network type; called by
// the segment placer 0x0063B830 and the subway tool's 0x00652980): AL 1
// when the drag's last step ends at the city's edge and connects there, its
// connection records appended to +0x100 and +0x10C; else AL 0. The last
// two anchor points are copied and drawn (slot 18 DrawNetworkLine, the
// NAM-guarded 0x0063AF40, through the vtable); equal points: 0. The side
// from the drag's direction (+0x7C / 2, or, for a one-cell irregular last
// step, turned by +0x80 - +0x7C), the cell across it from the last anchor
// must lie outside the city; the edge value 2 (or 1 / 3 from +0x80) on that
// side; for pens wider than one cell the neighbours simulator must answer
// the same for the step's two end cells; single-cell edge values need a
// one-cell pen. The network type's connection kind (0x00AA84C4) and the
// neighbours simulator must be there; for kinds 3, 0xC and 8 a byte from
// the cell's position (across x = cells or z = -1) inverted unless the
// framework object 0x441E5070's slot 0x98 (0)'s slot 0x64 says yes. Every
// cell of the last step with a cell info must have the tool's network and
// on the side only edges of the same value (another network's only when
// its priority (0x00AA83B0) is not above the tool's or the tool's row's
// +0xFC has its bit), and the neighbours simulator's slot 0x2C (kind, the
// cell across) must answer 0. Then one record per cell of the step (or,
// for single-cell values, one for the clamped cell across) into both
// vectors (PushBack 0x00627100).
uint8_t __fastcall AddEdgeConnection(char* self, void*, uint32_t word, uint32_t type)
{
    static bool logged = false;
    FirstCall("AddEdgeConnection", logged);
    char* const typeRow = Row(type);
    const int32_t penX = At<int32_t>(typeRow, row::kPenWidthX);
    const int32_t penZ = At<int32_t>(typeRow, row::kPenWidthZ);
    char* const anchors = At<char*>(self, field::kAnchors);
    const int32_t anchorCount = static_cast<int32_t>(At<char*>(self, ofield::kAnchorsEnd) - anchors) >> 3;
    const char* const pair = anchors + static_cast<uint32_t>(anchorCount - 2) * 8;
    int32_t second[2] = {At<int32_t>(const_cast<char*>(pair), 0), At<int32_t>(const_cast<char*>(pair), 4)};
    int32_t last[2] = {At<int32_t>(const_cast<char*>(pair), 8), At<int32_t>(const_cast<char*>(pair), 12)};
    void* const neighbors = Global<void*>(addr::SC4_MonthEndService);
    Virtual<void>(self, tslot::kDrawNetworkLine, second, last, word, type);
    if (second[0] == last[0] && second[1] == last[1])
        return 0;
    const int32_t direction7C = At<int32_t>(self, ofield::kDirection7C);
    uint32_t side = static_cast<uint32_t>(direction7C >> 1);
    char* const lastStep = At<char*>(self, ofield::kStepsEnd) - step::kSize;
    char* const cellA = Cells(self) + At<uint32_t>(lastStep, 0) * kPointSize;
    char* const cellB = Cells(self) + At<uint32_t>(lastStep, step::kLastCell) * kPointSize;
    uint32_t nx, nz, edgeValue, shift;
    if (At<int32_t>(self, ofield::kWord74) >= At<int32_t>(self, field::kStepCount)) {
        nx = static_cast<uint32_t>(EdgeSideDx(side)) + static_cast<uint32_t>(last[0]);
        nz = static_cast<uint32_t>(EdgeSideDz(side)) + static_cast<uint32_t>(last[1]);
        if (nx < At<uint32_t>(self, field::kCellsX) && nz < At<uint32_t>(self, field::kCellsZ))
            return 0;
        shift = side * 8;
        edgeValue = 2u << shift;
        if (penX > 1) {
            if (side & 1) {
                const uint32_t r1 = NeighborAt(neighbors, At<uint32_t>(cellA, 0), nz);
                const uint32_t r2 = NeighborAt(neighbors, At<uint32_t>(cellB, 0), nz);
                if (r1 != r2)
                    return 0;
            } else {
                const uint32_t r1 = NeighborAt(neighbors, nx, At<uint32_t>(cellA, 4));
                const uint32_t r2 = NeighborAt(neighbors, nx, At<uint32_t>(cellB, 4));
                if (r1 != r2)
                    return 0;
            }
        }
    } else {
        if (penZ != 1)
            return 0;
        const uint32_t span = At<uint32_t>(lastStep, step::kLastCell) - At<uint32_t>(lastStep, 0) + 1;
        if (span <= 1)
            side = (At<uint32_t>(self, ofield::kDirection80) - static_cast<uint32_t>(direction7C) + side) & 3u;
        nx = static_cast<uint32_t>(EdgeSideDx(side)) + static_cast<uint32_t>(last[0]);
        nz = static_cast<uint32_t>(EdgeSideDz(side)) + static_cast<uint32_t>(last[1]);
        if (nx < At<uint32_t>(self, field::kCellsX) && nz < At<uint32_t>(self, field::kCellsZ))
            return 0;
        const int32_t turn = At<int32_t>(self, ofield::kDirection80) - static_cast<int32_t>(side * 2);
        const uint32_t value = (turn >= 0 && turn <= 4) ? 3u : 1u;
        shift = side * 8;
        edgeValue = value << shift;
        if (penX > 1) {
            const uint32_t r1 = NeighborAt(neighbors, At<uint32_t>(cellA, 0), nz);
            const uint32_t r2 = NeighborAt(neighbors, At<uint32_t>(cellB, 0), nz);
            if (r1 != r2)
                return 0;
        }
    }
    const uint32_t singles = edgeValue & 0x01010101u;
    if (singles && penZ > 1)
        return 0;
    const uint32_t netType = At<uint32_t>(self, field::kType);
    void* const simulator = Global<void*>(addr::SC4_MonthEndService);
    const uint32_t kind = NeighborKind(netType);
    if (!simulator || !kind)
        return 0;
    uint8_t across = 0;
    if (kind == 3 || kind == 0xC || kind == 8) {
        void* reference = nullptr;
        void** const held = CallThis<void**>(addr::SC4RefPtr_FromFramework441E5070, &reference);
        void* const object = *held;
        CallThis<void>(addr::SC4_ReleaseRef2, &reference);
        across = (nx == At<uint32_t>(self, field::kCellsX) || nz == 0xFFFFFFFFu) ? 1 : 0;
        void* const inner = Virtual<void*>(object, 0x98, 0u);
        if (!Virtual<uint8_t>(inner, 0x64))
            across = across == 0 ? 1 : 0;
    }
    for (uint32_t c = At<uint32_t>(At<char*>(self, ofield::kStepsEnd) - step::kSize, 0);
         c <= At<uint32_t>(At<char*>(self, ofield::kStepsEnd) - step::kSize, step::kLastCell); ++c) {
        char* const info = Entry(self, PointKey(Cells(self) + c * kPointSize));
        if (info) {
            if (!(At<uint32_t>(info, cell::kFlags) & TypeFlag(At<uint32_t>(self, field::kType))))
                return 0;
            if (CallThis<uint8_t, uint32_t>(addr::cSC4NetworkTool_EdgeByte, info + cell::kCombined, side)) {
                const uint8_t wanted = static_cast<uint8_t>(edgeValue >> shift);
                for (uint32_t other = 0; other < kTypes; ++other) {
                    const uint8_t edge = CallThis<uint8_t, uint32_t>(addr::cSC4NetworkTool_EdgeByte,
                                                                     info + cell::kByType + other * 4, side);
                    if (!edge)
                        continue;
                    if (edge != wanted)
                        return 0;
                    const uint32_t own = At<uint32_t>(self, field::kType);
                    if (other == own || EdgePriority(own) >= EdgePriority(other))
                        continue;
                    if (!(At<uint32_t>(Row(own), orow::kCrossTypes) & (1u << other)))
                        return 0;
                }
            }
        }
        const uint32_t cx = At<uint32_t>(Cells(self) + c * kPointSize, 0);
        const uint32_t cz = At<uint32_t>(Cells(self) + c * kPointSize, 4);
        if (nx == 0xFFFFFFFFu || nx == At<uint32_t>(self, field::kCellsX)) {
            if (Virtual<uint32_t>(simulator, xslot::kNeighborConnectionAt, kind, nx, cz))
                return 0;
        } else if (nz == 0xFFFFFFFFu || nz == At<uint32_t>(self, field::kCellsZ)) {
            if (Virtual<uint32_t>(simulator, xslot::kNeighborConnectionAt, kind, cx, nz))
                return 0;
        }
    }
    // The records (0x18 bytes; +0x15 to +0x17 are the game's stack leftovers,
    // zeros in ours).
    uint32_t record[6] = {0, edgeValue, 0, 0, kind, 0};
    if (!singles) {
        const int32_t first = At<int32_t>(At<char*>(self, ofield::kStepsEnd) - step::kSize, 0);
        const int32_t lastCell = At<int32_t>(At<char*>(self, ofield::kStepsEnd) - step::kSize, step::kLastCell);
        for (int32_t c = first; c <= lastCell; ++c) {
            char* const point = Cells(self) + static_cast<uint32_t>(c) * kPointSize;
            uint32_t px = nx, pz = nz;
            if (nx == 0xFFFFFFFFu || nx == At<uint32_t>(self, field::kCellsX))
                pz = At<uint32_t>(point, 4);
            else if (nz == 0xFFFFFFFFu || nz == At<uint32_t>(self, field::kCellsZ))
                px = At<uint32_t>(point, 0);
            record[0] = PointKey(point);
            record[2] = px;
            record[3] = pz;
            record[5] = c == first ? across : (across == 0 ? 1u : 0u);
            CallThis<void, const uint32_t*>(addr::SC4CornerVector_PushBack, self + ofield::kEdgeConnections, record);
            CallThis<void, const uint32_t*>(addr::SC4CornerVector_PushBack, self + field::kNeighborConnections, record);
        }
        return 1;
    }
    if (penZ != 1)
        return 0;
    uint32_t cx = nx, cz = nz;
    if (static_cast<int32_t>(nx) < 0)
        cx = 0;
    else if (nx == At<uint32_t>(self, field::kCellsX))
        cx = nx - 1;
    if (static_cast<int32_t>(nz) < 0)
        cz = 0;
    else if (nz == At<uint32_t>(self, field::kCellsZ))
        cz = nz - 1;
    record[0] = (cz << 16) + cx;
    record[2] = nx;
    record[3] = nz;
    record[5] = 0;
    CallThis<void, const uint32_t*>(addr::SC4CornerVector_PushBack, self + ofield::kEdgeConnections, record);
    CallThis<void, const uint32_t*>(addr::SC4CornerVector_PushBack, self + field::kNeighborConnections, record);
    return 1;
}

// ====================================================================================
// Slot 20
// ====================================================================================

// 0x00625640 (slot 20, no arguments): the cost of the solution. For each
// solved cell with a cell info: when it is in the placement (+0x10) and not
// marked (+0x52), the largest cell cost (+0xDC of the rows of its
// networks); and its four corners' height differences from the terrain
// (|t - h|) summed as a float. Plus for each major bridge (last - first) *
// (record +0x154 + the tool's +0x320) + record +0x150, for each +0xE4
// record (last - first) * +0x324, for each intersection its RUL0 rule's
// +0xB4 (a rule missing from the store makes the game read address 0xB4:
// ours too). EAX that sum + the highlight cost's low dword (+0x220) + the
// height sum times +0x328 rounded to an integer (the float's bits after
// adding 12582912.0f, less 0x4B400000).
int32_t __fastcall GetCostOfSolution(char* self, void*)
{
    static bool logged = false;
    FirstCall("GetCostOfSolution", logged);
    uint32_t total = 0;
    float heights = 0.0f;
    char* const solvedEnd = At<Vector>(self, field::kSolved).end;
    for (char* at = At<Vector>(self, field::kSolved).begin; at != solvedEnd; at += 12) {
        char* const info = Entry(self, At<uint32_t>(at, 8));
        if (!info)
            continue;
        if (At<uint8_t>(info, cell::kInPlacement) && !At<uint8_t>(info, cell::kHasFlag100000)) {
            const uint32_t flags = At<uint32_t>(info, cell::kFlags);
            uint32_t largest = 0;
            for (uint32_t type = 0; type < kTypes; ++type) {
                if (!(flags & TypeFlag(type)))
                    continue;
                const uint32_t cost = At<uint32_t>(Row(type), orow::kCellCost);
                if (largest < cost)
                    largest = cost;
            }
            total += largest;
        }
        for (std::size_t k = 0; k < 4; ++k) {
            const double terrain = Virtual<double>(Terrain(self), xslot::kTerrainVertexAltitude,
                                                   At<uint32_t>(info, cell::kVertices + 4 * k));
            const double height = static_cast<double>(At<float>(info, cell::kHeights + 4 * k));
            // `fcomp; test ah, 0x41; jne`: t - h only when above (ordered).
            const double difference = terrain > height ? terrain - height : height - terrain;
            heights = static_cast<float>(difference + static_cast<double>(heights));
        }
    }
    const uint32_t bridgeStep = At<uint32_t>(self, ofield::kBridgeStepCost);
    char* const bridgesEnd = At<char*>(self, ofield::kBridgesEnd);
    for (char* bridge = At<char*>(self, field::kMajorBridges); bridge != bridgesEnd; bridge += omajor::kSize) {
        char* const recordAt = At<char*>(Row(At<uint32_t>(bridge, omajor::kType)), row::kBridges) +
                               At<uint32_t>(bridge, omajor::kRecord) * static_cast<uint32_t>(bridge::kSize);
        total += (At<uint32_t>(bridge, omajor::kLast) - At<uint32_t>(bridge, omajor::kFirst)) *
                     (At<uint32_t>(recordAt, obridge::kCostPerStep) + bridgeStep) +
                 At<uint32_t>(recordAt, obridge::kCostBase);
    }
    const uint32_t recordStep = At<uint32_t>(self, ofield::kRecordStepCost);
    char* const recordsEnd = At<char*>(self, ofield::kRecordsEnd);
    for (char* record = At<char*>(self, field::kVectorE4); record != recordsEnd; record += orecord::kSize)
        total += (At<uint32_t>(record, orecord::kLast) - At<uint32_t>(record, orecord::kFirst)) * recordStep;
    char* const intersectionsEnd = At<char*>(self, ofield::kIntersectionsEnd);
    for (char* at = At<char*>(self, field::kIntersections); at != intersectionsEnd; at += oisect::kSize)
        total += Read<uint32_t>(StoreRule(At<uint32_t>(at, oisect::kRule)), orule::kCost);
    const double scale = static_cast<double>(At<float>(self, ofield::kHeightCostScale));
    const float rounded = static_cast<float>(static_cast<double>(heights) * scale +
                                             static_cast<double>(Global<float>(addr::cSC4NetworkTool_CostRoundMagic)));
    return static_cast<int32_t>(At<uint32_t>(self, ofield::kHighlightCost) + Bits(rounded) + total - 0x4B400000u);
}

// ====================================================================================
// The major bridges' cell vectors
// ====================================================================================

constexpr int32_t kCellSize = static_cast<int32_t>(bcell::kSize);

// 0x00626750 (ret 4: the source; this the destination; called by
// CopyRange): a bridge cell assigned: the words +0x0 to +0xC, the byte
// +0x10, the words +0x14 to +0x3C, the byte +0x40 (the padding after the
// bytes not copied), the vector of 32-byte elements +0x44 by its operator=
// 0x00625270. EAX this.
char* __fastcall BridgeCellAssign(char* self, void*, char* source)
{
    static bool logged = false;
    FirstCall("BridgeCellAssign", logged);
    for (std::size_t at = 0; at < bcell::kByte10; at += 4)
        At<uint32_t>(self, at) = At<uint32_t>(source, at);
    At<uint8_t>(self, bcell::kByte10) = At<uint8_t>(source, bcell::kByte10);
    for (std::size_t at = 0x14; at < bcell::kByte40; at += 4)
        At<uint32_t>(self, at) = At<uint32_t>(source, at);
    At<uint8_t>(self, bcell::kByte40) = At<uint8_t>(source, bcell::kByte40);
    CallThis<char*, char*>(addr::SC4Vector32_Assign, self + bcell::kVector, source + bcell::kVector);
    return self;
}

// 0x006272A0 (cdecl: first, last, the destination; two more arguments the
// callers push and nobody reads; called by the operator=): (last - first)
// / 0x50 cells assigned in order (Assign); EAX the destination's end.
char* __cdecl BridgeCellCopyRange(char* first, char* last, char* destination)
{
    static bool logged = false;
    FirstCall("BridgeCellCopyRange", logged);
    const int32_t count = static_cast<int32_t>(last - first) / kCellSize;
    if (count <= 0)
        return destination;
    for (int32_t left = count; left; --left) {
        CallThis<char*, char*>(addr::SC4BridgeCell_Assign, destination, first);
        first += kCellSize;
        destination += kCellSize;
    }
    return destination;
}

// 0x0062A420 (ret 0xC: a count, first, last; this not read; called by the
// operator=): memory for the count's cells (SC4::stl_allocate, none for 0)
// and the range copy-constructed into it (0x006267D0, skipped at a null
// destination); EAX the memory.
char* __fastcall BridgeCellAllocateCopy(void*, void*, uint32_t count, char* first, char* last)
{
    static bool logged = false;
    FirstCall("BridgeCellAllocateCopy", logged);
    char* const memory =
        count ? CallCdecl<char*, uint32_t>(addr::SC4_stl_allocate, count * static_cast<uint32_t>(kCellSize)) : nullptr;
    if (first == last)
        return memory;
    const std::uintptr_t offset = reinterpret_cast<std::uintptr_t>(first) - reinterpret_cast<std::uintptr_t>(memory);
    std::uintptr_t to = reinterpret_cast<std::uintptr_t>(memory);
    do {
        if (to)
            CallThis<char*, char*>(addr::SC4BridgeCell_CopyConstruct, reinterpret_cast<char*>(to),
                                   reinterpret_cast<char*>(to + offset));
        to += kCellSize;
    } while (to + offset != reinterpret_cast<std::uintptr_t>(last));
    return memory;
}

// 0x0062BEF0 (cdecl: first, last; called by the operator=): each cell's
// vector +0x44 freed when it has memory.
void __cdecl BridgeCellDestroyRange(char* first, char* last)
{
    static bool logged = false;
    FirstCall("BridgeCellDestroyRange", logged);
    if (first == last)
        return;
    char* at = first;
    do {
        if (void* const memory = At<void*>(at, bcell::kVector))
            CallCdecl<void, void*>(addr::SC4_Free, memory);
        at += kCellSize;
    } while (at != last);
}

// 0x0062C1E0 (ret 4: the other vector; this a vector of bridge cells, a
// major bridge's +0x1C; called by the major bridges' copy 0x0062C510): the
// STL operator=: nothing for itself; more cells than the capacity: new
// memory with copies (AllocateCopy), the old freed (0x0062BF20); no more
// than the size: assigned (CopyRange) and the rest destroyed
// (DestroyRange); else the size assigned and the rest copy-constructed
// (0x0062F6B0). The end at begin + the other's count. EAX this.
char* __fastcall BridgeCellsAssign(char* self, void*, char* other)
{
    static bool logged = false;
    FirstCall("BridgeCellsAssign", logged);
    if (other == self)
        return self;
    Vector& mine = At<Vector>(self, 0);
    char* const otherBegin = At<Vector>(other, 0).begin;
    char* const otherEnd = At<Vector>(other, 0).end;
    const uint32_t count = static_cast<uint32_t>(static_cast<int32_t>(otherEnd - otherBegin) / kCellSize);
    const uint32_t capacity = static_cast<uint32_t>(static_cast<int32_t>(mine.capacity - mine.begin) / kCellSize);
    uint32_t tag = 0;
    if (count > capacity) {
        char* const memory = CallThis<char*, uint32_t, char*, char*>(addr::SC4BridgeCellVector_AllocateCopy, self,
                                                                      count, otherBegin, otherEnd);
        CallThis<void>(addr::SC4BridgeCellVector_Free, self);
        mine.begin = memory;
        mine.capacity = memory + count * static_cast<uint32_t>(kCellSize);
    } else {
        const uint32_t size = static_cast<uint32_t>(static_cast<int32_t>(mine.end - mine.begin) / kCellSize);
        if (size >= count) {
            char* const newEnd = CallCdecl<char*, char*, char*, char*, uint32_t*, uint32_t>(
                addr::SC4BridgeCell_CopyRange, otherBegin, otherEnd, mine.begin, &tag, 0u);
            CallCdecl<void, char*, char*>(addr::SC4BridgeCell_DestroyRange, newEnd, mine.end);
        } else {
            CallCdecl<char*, char*, char*, char*, uint32_t*, uint32_t>(
                addr::SC4BridgeCell_CopyRange, otherBegin, otherBegin + size * static_cast<uint32_t>(kCellSize),
                mine.begin, &tag, 0u);
            const uint32_t sizeNow = static_cast<uint32_t>(static_cast<int32_t>(mine.end - mine.begin) / kCellSize);
            CallCdecl<char*, char*, char*, char*, uint32_t*>(
                addr::SC4BridgeCell_UninitializedCopy,
                At<Vector>(other, 0).begin + sizeNow * static_cast<uint32_t>(kCellSize), At<Vector>(other, 0).end,
                mine.end, &tag);
        }
    }
    mine.end = mine.begin + count * static_cast<uint32_t>(kCellSize);
    return self;
}

// ====================================================================================
// Installation
// ====================================================================================

#define OPENSC4_DETOUR(address, fn) OPENSC4_NETTOOL_DETOUR(address, fn)

const Detour kRuleOccupants[] = {
    OPENSC4_DETOUR(addr::cSC4NetworkTool_PlaceRuleOccupants, PlaceRuleOccupants),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_PlaceRuleCellOccupant, PlaceRuleCellOccupant),
    OPENSC4_DETOUR(addr::SC4Rul0Rule_ConsCellAt, ConsCellAt),
    OPENSC4_DETOUR(addr::SC4Rul0ConsTable_Find, ConsTableFind),
};

const Detour kTunnels[] = {
    OPENSC4_DETOUR(addr::cSC4NetworkTool_PlaceTunnels, PlaceTunnels),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_MakeTunnelPiece, MakeTunnelPiece),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_AddEdgeConnection, AddEdgeConnection),
};

const Detour kCost[] = {
    OPENSC4_DETOUR(addr::cSC4NetworkTool_GetCostOfSolution, GetCostOfSolution),
};

const Detour kBridgeCells[] = {
    OPENSC4_DETOUR(addr::SC4BridgeCell_Assign, BridgeCellAssign),
    OPENSC4_DETOUR(addr::SC4BridgeCell_CopyRange, BridgeCellCopyRange),
    OPENSC4_DETOUR(addr::SC4BridgeCellVector_AllocateCopy, BridgeCellAllocateCopy),
    OPENSC4_DETOUR(addr::SC4BridgeCell_DestroyRange, BridgeCellDestroyRange),
    OPENSC4_DETOUR(addr::SC4BridgeCellVector_Assign, BridgeCellsAssign),
};

#undef OPENSC4_DETOUR

bool InstallRuleOccupants(registry::Mode) { return InstallDetours(kRuleOccupants, "ruleoccupants"); }
bool InstallTunnels(registry::Mode) { return InstallDetours(kTunnels, "tunnels"); }
bool InstallCost(registry::Mode) { return InstallDetours(kCost, "cost"); }
bool InstallBridgeCells(registry::Mode) { return InstallDetours(kBridgeCells, "bridgecells"); }

const bool g_registered[] = {
    registry::Register({"simulators.nettool.ruleoccupants", "simulators/nettool", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4NetworkTool_PlaceRuleOccupants,
                        "Our network tool's RUL0 intersection occupants: PlaceRuleOccupants, PlaceRuleCellOccupant, "
                        "the rule's cons cell lookup and its table's find (4 detours; "
                        "src/core/nettool_occupants_2.cpp)",
                        false, InstallRuleOccupants}),
    registry::Register({"simulators.nettool.tunnels", "simulators/nettool", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4NetworkTool_PlaceTunnels,
                        "Our network tool's tunnels and city-edge connections: PlaceTunnels, MakeTunnelPiece (the "
                        "entrances and the terrain around them) and AddEdgeConnection (3 detours; "
                        "src/core/nettool_occupants_2.cpp)",
                        false, InstallTunnels}),
    registry::Register({"simulators.nettool.cost", "simulators/nettool", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4NetworkTool_GetCostOfSolution,
                        "Our network tool's slot 20 GetCostOfSolution (1 detour; src/core/nettool_occupants_2.cpp)",
                        false, InstallCost}),
    registry::Register({"simulators.nettool.bridgecells", "simulators/nettool", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::SC4BridgeCellVector_Assign,
                        "Our network tool's major bridge cell vectors: the cell's Assign, CopyRange, AllocateCopy, "
                        "DestroyRange and the vector's operator= (5 detours; src/core/nettool_occupants_2.cpp)",
                        false, InstallBridgeCells}),
};

} // namespace
} // namespace opensc4::nettool_sim
