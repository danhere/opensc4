// Our cSC4NetworkTool, part t6, second file (build 641; the class map and
// the fields are in src/core/nettool_sim.h, this part's layouts in
// nettool_place.h): the cell passes of the segment placer, the edge word
// helpers and the intersections' template code. Three groups:
// simulators.nettool.placecells (9 detours), simulators.nettool.edgewords
// (4) and simulators.nettool.isectvector (8). The analysis of the whole part
// (what stays the game's, NAM, registers, wrappers, floats, stack leftovers,
// the call list) is at the top of src/core/nettool_place_1.cpp.
//
// The one thing here C++ cannot repeat: FindTunnelEnd's last test takes
// the tangent of the parameters' float +0x2AC with FPTAN and compares the
// unrounded product with the best difference, so X87TunnelSlope runs the
// game's instructions (0x00638940 to 0x0063895D) and gives the status word.
#include "core/nettool_place.h"

#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace opensc4::nettool_sim {
namespace {

using namespace place;

// The seven words (+0x0 to +0x18) every intersection copy moves first.
constexpr std::size_t kIsectWords = isect::kWords;
// The six words before a major bridge's byte +0x18.
constexpr std::size_t kBridgeWords = 6;
constexpr uint32_t kWideSideKind = 4;

// The next node of the game's red-black tree (STLport's increment, as the
// preview drawable's walk inlines it): the leftmost node of the right
// subtree; else up while the node is its parent's right child, the parent
// unless the node's right child is that parent (the head's case).
char* TreeNext(char* node)
{
    if (char* right = At<char*>(node, tree::kRight)) {
        node = right;
        while (char* const left = At<char*>(node, tree::kLeft))
            node = left;
        return node;
    }
    char* parent = At<char*>(node, tree::kParent);
    while (node == At<char*>(parent, tree::kRight)) {
        node = parent;
        parent = At<char*>(parent, tree::kParent);
    }
    return At<char*>(node, tree::kRight) == parent ? node : parent;
}

inline uint32_t CellKey(char* self, uint32_t c)
{
    char* const at = Cells(self) + c * kPointSize;
    return (At<uint32_t>(at, 4) << 16) + At<uint32_t>(at, 0);
}

// A vertex index of a cell as the drag code computes it (row z, x; the
// row stride +0x248).
inline uint32_t VertexOf(char* self, uint32_t c)
{
    char* const at = Cells(self) + c * kPointSize;
    return At<uint32_t>(at, 4) * At<uint32_t>(self, field::kVertexStride) + At<uint32_t>(at, 0);
}

inline uint32_t VertexOffset(char* self, std::size_t table, int32_t direction)
{
    return At<uint32_t>(self, table + static_cast<std::size_t>(static_cast<uint32_t>(direction)) * 4);
}

// Four terrain altitudes (slot 0x30, in this order) summed as the tunnel
// code does: the first stored as a float, each next added to the stored
// float and stored again, the last added and the sum times the float
// 0.25 at 0x00A8821C, unrounded (the caller stores it as a float or
// compares it first).
double FourAltitudes(void* terrain, uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
    const float s0 = static_cast<float>(VertexAltitude(terrain, a));
    const float s1 = static_cast<float>(VertexAltitude(terrain, b) + static_cast<double>(s0));
    const float s2 = static_cast<float>(VertexAltitude(terrain, c) + static_cast<double>(s1));
    return (VertexAltitude(terrain, d) + static_cast<double>(s2)) *
           static_cast<double>(Global<float>(addr::cSC4NetworkOccupant_PositionScale));
}

// 0x00638935 to 0x00638963: FLD the angle, FPTAN, FSTP ST(0), FMUL the
// scale, FIMUL the span, FCOMP the best: the status word (FPTAN cannot be
// repeated in C++, and the product is compared unrounded).
__declspec(naked) uint32_t __cdecl X87TunnelSlope(const float* /*angle*/, const float* /*scale*/,
                                                  const int32_t* /*span*/, const float* /*best*/)
{
    __asm {
        mov eax, [esp + 4]
        fld dword ptr [eax]
        fptan
        fstp st(0)
        mov eax, [esp + 8]
        fmul dword ptr [eax]
        mov eax, [esp + 12]
        fimul dword ptr [eax]
        mov eax, [esp + 16]
        fcomp dword ptr [eax]
        fnstsw ax
        movzx eax, ax
        ret
    }
}

// A step's first and last cell.
inline uint32_t FirstCell(char* self, int32_t i) { return At<uint32_t>(Steps(self) + i * step::kSize, 0); }
inline uint32_t LastCell(char* self, int32_t i) { return At<uint32_t>(Steps(self) + i * step::kSize, step::kLastCell); }

// ====================================================================================
// The edge word helpers
// ====================================================================================

// 0x00620F00 (ret 4: another edge word's address; this an edge word):
// nothing when the other word is 0; else per side byte (low three bits):
// kind 0 takes the other's kind (ORed in), kind 1 becomes 2 (bits 0 and 1
// flipped, bit 1 set) when the other's is 3 or 2, kind 3 becomes 2 (bit 0
// flipped, bit 1 set) when the other's is 1 or 2; the word stored once.
void __fastcall MergeSideKinds(uint32_t* word, void*, const uint32_t* other)
{
    static bool logged = false;
    FirstCall("MergeSideKinds", logged);
    const uint32_t from = *other;
    if (!from)
        return;
    uint32_t value = *word;
    for (uint32_t shift = 0; shift < 32; shift += 8) {
        const uint32_t mine = (value >> shift) & 7;
        const uint32_t theirs = (from >> shift) & 7;
        if (mine == 0) {
            value |= theirs << shift;
        } else if (mine == 1) {
            if (theirs == 3 || theirs == 2)
                value = (value ^ (3u << shift)) | (2u << shift);
        } else if (mine == 3) {
            if (theirs == 1 || theirs == 2)
                value = (value ^ (1u << shift)) | (2u << shift);
        }
    }
    *word = value;
}

// 0x00620FC0 (ret 8: a side, a byte; this an edge word): that side's byte
// (the shift side * 8, masked to five bits as the processor does)
// replaced; EAX this (SetSideKind 0x006335B0 hands it on).
uint32_t* __fastcall SetSideByte(uint32_t* word, void*, uint32_t side, uint32_t byte)
{
    static bool logged = false;
    FirstCall("SetSideByte", logged);
    const uint32_t shift = (side << 3) & 31;
    *word = (~(0xFFu << shift) & *word) | (static_cast<uint32_t>(static_cast<uint8_t>(byte)) << shift);
    return word;
}

// 0x00620FF0 (ret 8: a side, a direction byte; this an edge word): the
// side's byte becomes its own bit 2 plus the kind {2, 3, 0, 1, 2, 3, 0,
// 1}[(direction + 2 * side) & 7] (the table on the game's stack).
void __fastcall SetSideFromDirection(uint32_t* word, void*, uint32_t side, uint32_t direction)
{
    static bool logged = false;
    FirstCall("SetSideFromDirection", logged);
    static constexpr uint8_t kKinds[8] = {2, 3, 0, 1, 2, 3, 0, 1};
    const uint32_t shift = (side << 3) & 0xFF & 31;
    const uint32_t index = (static_cast<uint32_t>(static_cast<uint8_t>(direction)) + side * 2) & 7;
    const uint8_t kind = static_cast<uint8_t>((((*word >> shift) >> 2) & 1) << 2) | kKinds[index];
    *word = (~(0xFFu << shift) & *word) | (static_cast<uint32_t>(kind) << shift);
}

// 0x00621440 (no arguments; this an edge word): each side byte reduced to
// its low three bits, stored after each byte as the game does.
void __fastcall ClearSideHighBits(uint32_t* word, void*)
{
    static bool logged = false;
    FirstCall("ClearSideHighBits", logged);
    *word = (*word & 0xFFFFFF00u) | (*word & 7u);
    *word = (*word & 0xFFFF00FFu) | (((*word >> 8) & 7u) << 8);
    *word = (*word & 0xFF00FFFFu) | (((*word >> 16) & 7u) << 16);
    *word = (*word & 0x00FFFFFFu) | (((*word >> 24) & 7u) << 24);
}

// ====================================================================================
// The cell passes
// ====================================================================================

// 0x00620930 (ret 0xC: a cell point, four heights, a network lot byte;
// this the preview drawable): VertexHeights (record, point, heights, byte)
// for each node of the cell tree (+0x10) in the key's EqualRange, then for
// each node of the textured tree (+0x1C) from its lower bound to its upper
// bound (the searches inlined; unsigned keys (z << 16) + x).
void __fastcall UpdateCellHeights(char* preview, void*, const uint32_t* point, void* heights, uint32_t lot)
{
    static bool logged = false;
    FirstCall("UpdateCellHeights", logged);
    uint32_t key = (point[1] << 16) + point[0];
    char* range[2] = {};
    CallThis<void, char**, const uint32_t*>(addr::SC4Multimap_EqualRange, preview + preview::kCells, range, &key);
    for (char* node = range[0]; node != range[1]; node = TreeNext(node))
        CallThis<void, char*, const uint32_t*, void*, uint32_t>(addr::cSC4NetworkToolPreview_VertexHeights, preview,
                                                                node + tree::kValue + 4, point, heights, lot);
    const uint32_t wanted = (point[1] << 16) + point[0];
    char* const head = At<char*>(preview, preview::kTextured);
    char* upper = head;
    for (char* node = At<char*>(head, tree::kParent); node;) {
        if (wanted >= At<uint32_t>(node, tree::kValue)) {
            node = At<char*>(node, tree::kRight);
        } else {
            upper = node;
            node = At<char*>(node, tree::kLeft);
        }
    }
    char* lower = head;
    for (char* node = At<char*>(head, tree::kParent); node;) {
        if (At<uint32_t>(node, tree::kValue) < wanted) {
            node = At<char*>(node, tree::kRight);
        } else {
            lower = node;
            node = At<char*>(node, tree::kLeft);
        }
    }
    for (char* node = lower; node != upper; node = TreeNext(node))
        CallThis<void, char*, const uint32_t*, void*, uint32_t>(addr::cSC4NetworkToolPreview_VertexHeights, preview,
                                                                node + tree::kValue + 4, point, heights, lot);
}

// A changed cell's four heights from slot 73 (a network lot's vertices
// + 100000): each the float of the unrounded answer; true when one differs
// from the height before (a NaN differs).
bool UpdateHeights(char* self, char* info)
{
    const uint32_t offset = At<uint8_t>(info, cell::kNetworkLot) ? kLotVertexOffset : 0u;
    bool changed = false;
    for (uint32_t i = 0; i < 4; ++i) {
        const double h =
            Virtual<double>(self, pslot::kGetNetworkVertexHt, At<uint32_t>(info, cell::kVertices + i * 4) + offset);
        if (!(h == static_cast<double>(At<float>(info, cell::kHeights + i * 4))))
            changed = true;
        At<float>(info, cell::kHeights + i * 4) = static_cast<float>(h);
    }
    return changed;
}

// 0x00626000 (no arguments): for each cell of the changed set +0xA4 (Begin
// 0x004B0390, then the inlined increment), its heights (slot 73); when one
// changed and the cell has no solved cell, the preview drawable's
// UpdateCellHeights with its point, its heights and its network lot byte.
void __fastcall PreviewChangedCellHeights(char* self, void*)
{
    static bool logged = false;
    FirstCall("PreviewChangedCellHeights", logged);
    char* iterator[2] = {};
    CallThis<void, char**>(addr::SC4HashTable_Begin, self + pfield::kChanged, iterator);
    for (char* node = iterator[0]; node; node = HashNext(iterator[1], node)) {
        char* const info = Entry(self, At<uint32_t>(node, rul::table::kKey));
        if (UpdateHeights(self, info) && At<int32_t>(info, cell::kSolvedIndex) < 0) {
            const uint32_t point[2] = {At<uint32_t>(info, cell::kX), At<uint32_t>(info, cell::kZ)};
            CallThis<void, const uint32_t*, char*, uint32_t>(
                addr::cSC4NetworkToolPreview_UpdateCellHeights, At<char*>(self, field::kPreview), point,
                info + cell::kHeights, static_cast<uint32_t>(At<uint8_t>(info, cell::kNetworkLot)));
        }
    }
}

// The solved count, as the game divides (signed by 12).
inline int32_t SolvedCount(char* self)
{
    const Vector& solved = VectorAt(self, field::kSolved);
    return static_cast<int32_t>(solved.end - solved.begin) / 0xC;
}

// 0x00629CF0 (no arguments): for each cell of the changed set +0xA4, its
// heights (slot 73); a changed, occupied (+0x51) cell without a solved cell
// gets one from its network occupant (GetNetworkOccupantAt with the
// tool's filter +0x364; not tested for null, as the game's): the piece
// (slot 0x5C), the rotation byte (slot 0x68; the original's three bytes
// after it are its stack's, 0 here), the cell's key; its index first, the
// record appended in place or through SC4Vector12::InsertOverflow; the
// occupant released.
void __fastcall ResolveChangedCells(char* self, void*)
{
    static bool logged = false;
    FirstCall("ResolveChangedCells", logged);
    char* iterator[2] = {};
    CallThis<void, char**>(addr::SC4HashTable_Begin, self + pfield::kChanged, iterator);
    for (char* node = iterator[0]; node; node = HashNext(iterator[1], node)) {
        const uint32_t key = At<uint32_t>(node, rul::table::kKey);
        char* const info = Entry(self, key);
        if (!UpdateHeights(self, info) || At<int32_t>(info, cell::kSolvedIndex) >= 0 ||
            !At<uint8_t>(info, cell::kOccupied))
            continue;
        void* occupant = nullptr;
        CallThis<uint8_t, uint32_t, uint32_t, void*, void**>(
            addr::cSC4NetworkTool_GetNetworkOccupantAt, self, At<uint32_t>(info, cell::kX),
            At<uint32_t>(info, cell::kZ), At<void*>(self, field::kFilter), &occupant);
        const uint32_t piece = Virtual<uint32_t>(occupant, pxslot::kOccupantPiece);
        const uint8_t rotation = Virtual<uint8_t>(occupant, pxslot::kOccupantRotation);
        At<int32_t>(info, cell::kSolvedIndex) = SolvedCount(self);
        const uint32_t record[3] = {piece, rotation, key};
        Vector& solved = VectorAt(self, field::kSolved);
        char* const end = solved.end;
        if (end != solved.capacity) {
            if (end) {
                At<uint32_t>(end, 0) = record[0];
                At<uint32_t>(end, 4) = record[1];
                At<uint32_t>(end, 8) = record[2];
            }
            solved.end += 0xC;
        } else {
            uint8_t tag = 0;
            CallThis<void, char*, const uint32_t*, uint8_t*, uint32_t, uint32_t>(addr::SC4Vector12_InsertOverflow,
                                                                                &solved, end, record, &tag, 1u, 1u);
        }
        if (occupant)
            ReleaseOf(occupant);
    }
}

// 0x00629EB0 (ret 4: a uint32 vector of changed vertices, its begin and
// end taken once): for each vertex, the cells around it (x - 1 and z - 1
// down to 0, x and z up to the last cell; signed loops, keys (z << 16) +
// x): an occupied cell without a solved cell gets one from its network
// occupant (the occupant manager's first by cell with the tool's filter,
// asked for the network occupant interface: GetNetworkOccupantAt inlined,
// nothing outside the city, and no null test after, as the game's), its
// index first, its four heights from the first satisfier's results
// (0x0065EB00 per vertex), the record (piece, rotation byte with zeros
// above where the original has its stack's, key) appended in place or
// grown inline; the occupant released.
void __fastcall ResolveChangedVertices(char* self, void*, const Vector* vertices)
{
    static bool logged = false;
    FirstCall("ResolveChangedVertices", logged);
    char* const last = vertices->end;
    for (char* at = vertices->begin; at != last; at += 4) {
        const uint32_t stride = At<uint32_t>(self, field::kVertexStride);
        const uint32_t vertex = At<uint32_t>(at, 0);
        const uint32_t vx = vertex % stride, vz = vertex / stride;
        const int32_t minX = static_cast<int32_t>(vx) > 0 ? static_cast<int32_t>(vx) - 1 : 0;
        const int32_t minZ = static_cast<int32_t>(vz) > 0 ? static_cast<int32_t>(vz) - 1 : 0;
        const int32_t maxX =
            static_cast<int32_t>(vx < At<uint32_t>(self, field::kCellsX) ? vx : At<uint32_t>(self, field::kCellsX) - 1);
        const int32_t maxZ =
            static_cast<int32_t>(vz < At<uint32_t>(self, field::kCellsZ) ? vz : At<uint32_t>(self, field::kCellsZ) - 1);
        if (minZ > maxZ)
            continue;
        uint32_t rowKey = (static_cast<uint32_t>(minZ) << 16) + static_cast<uint32_t>(minX);
        for (int32_t z = minZ; z <= maxZ; ++z, rowKey += 0x10000) {
            if (minX > maxX)
                continue;
            uint32_t key = rowKey;
            for (int32_t x = minX; x <= maxX; ++x, ++key) {
                char* const info = Entry(self, key);
                if (!info || At<int32_t>(info, cell::kSolvedIndex) >= 0 || !At<uint8_t>(info, cell::kOccupied))
                    continue;
                void* occupant = nullptr;
                void* const filter = At<void*>(self, field::kFilter);
                if (static_cast<uint32_t>(x) < At<uint32_t>(self, field::kCellsX) &&
                    static_cast<uint32_t>(z) < At<uint32_t>(self, field::kCellsZ)) {
                    void* first = nullptr;
                    const uint8_t found = Virtual<uint8_t>(At<void*>(self, field::kOccupants),
                                                           occupants_slot::kGetFirstByCell, &first, x, z, filter);
                    if (found) {
                        Virtual<uint8_t>(first, 0, kIidNetworkOccupant, &occupant);
                        if (first)
                            ReleaseOf(first);
                    } else if (first) {
                        ReleaseOf(first);
                    }
                }
                const uint32_t piece = Virtual<uint32_t>(occupant, pxslot::kOccupantPiece);
                const uint8_t rotation = Virtual<uint8_t>(occupant, pxslot::kOccupantRotation);
                At<int32_t>(info, cell::kSolvedIndex) = SolvedCount(self);
                for (uint32_t i = 0; i < 4; ++i)
                    CallThis<uint8_t, uint32_t, char*>(addr::SC4VertexHeightSatisfier_GetResult,
                                                       self + field::kSatisfier1,
                                                       At<uint32_t>(info, cell::kVertices + i * 4),
                                                       info + cell::kHeights + i * 4);
                const uint32_t record[3] = {piece, rotation, key};
                PushRecord12(VectorAt(self, field::kSolved), record);
                if (occupant)
                    ReleaseOf(occupant);
            }
        }
    }
}

// The one-way bits of a side: the byte of the table 0x00AA8BD8 at the
// index (a direction word plus 8 for an odd side) shifted by twice the
// side into the cell's one-way byte, its two bits there replaced (an 8-bit
// add, as the game's).
inline void OneWayBits(char* info, uint32_t index, uint32_t shift)
{
    const uint8_t bits = Global<uint8_t>(addr::cSC4NetworkTool_OneWaySideBits + index);
    const uint8_t mask = static_cast<uint8_t>(3u << (shift & 31));
    At<uint8_t>(info, cell::kOneWay) =
        static_cast<uint8_t>(static_cast<uint8_t>(bits << (shift & 31)) + (static_cast<uint8_t>(~mask) &
                                                                           At<uint8_t>(info, cell::kOneWay)));
}

inline void SetSide(uint32_t* edges, uint32_t side, uint32_t direction)
{
    CallThis<void, uint32_t, uint32_t>(addr::cSC4NetworkTool_SetSideFromDirection, edges, side, direction);
}

// 0x00638160 (ret 4: a network type): for each valid step (the count
// re-read) and each of its cells (the last re-read), an edge word built
// from 0: past the regular steps, each side whose neighbour (the steps
// 0x00AA8B74 / 0x00AA8B84) is in the placement (+0x10) gets the second
// direction's kind (0x00620FF0 with the byte +0x80); within them, before
// the last valid step the side half the first direction (+0x7C / 2, signed)
// gets the first direction (or the second from the last regular step on)
// and after the first step the opposite side ((half + 2) % 4) the first
// direction's byte. For type 10 (one-way road) each such side also sets
// the cell's one-way bits. Then the cell's edge word for the type has its
// high bits cleared (0x00621440), the built word merged in (0x00620F00),
// CombineEdges. The cell entry is not tested for null (as the game's).
void __fastcall SetStepEdges(char* self, void*, uint32_t type)
{
    static bool logged = false;
    FirstCall("SetStepEdges", logged);
    const int32_t lastRegular = At<int32_t>(self, pfield::kRegularSteps) - 1;
    const int32_t valid = At<int32_t>(self, field::kValidSteps);
    const int32_t lastValid = valid - 1;
    const int32_t half = At<int32_t>(self, pfield::kDirection1) / 2;
    const int32_t opposite = (half + 2) % 4;
    const bool oneWay = type == kTypeOneWay;
    if (valid <= 0)
        return;
    for (int32_t i = 0; i < At<int32_t>(self, field::kValidSteps); ++i) {
        for (uint32_t c = FirstCell(self, i); c <= LastCell(self, i); ++c) {
            char* const at = Cells(self) + c * kPointSize;
            const uint32_t x = At<uint32_t>(at, 0), z = At<uint32_t>(at, 4);
            char* const info = Entry(self, (z << 16) + x);
            uint32_t edges = 0;
            if (i >= At<int32_t>(self, pfield::kRegularSteps)) {
                for (uint32_t side = 0; side < 4; ++side) {
                    const uint32_t key =
                        ((Global<uint32_t>(addr::cSC4NetworkTool_StepSideDz + side * 4) + z) << 16) +
                        Global<uint32_t>(addr::cSC4NetworkTool_StepSideDx + side * 4) + x;
                    char* const next = Entry(self, key);
                    if (!next || !At<uint8_t>(next, cell::kInPlacement))
                        continue;
                    SetSide(&edges, side, static_cast<uint32_t>(At<uint8_t>(self, pfield::kDirection2)));
                    if (oneWay)
                        OneWayBits(info, At<uint32_t>(self, pfield::kDirection2) + (side & 1) * 8, side * 2);
                }
            } else {
                if (i < lastValid) {
                    const uint32_t direction = i < lastRegular ? At<uint32_t>(self, pfield::kDirection1)
                                                               : At<uint32_t>(self, pfield::kDirection2);
                    SetSide(&edges, static_cast<uint32_t>(half), direction);
                    if (oneWay)
                        OneWayBits(info,
                                   At<uint32_t>(self, pfield::kDirection1) + (static_cast<uint32_t>(half) & 1) * 8,
                                   static_cast<uint32_t>(half) * 2);
                }
                if (i > 0) {
                    SetSide(&edges, static_cast<uint32_t>(opposite),
                            static_cast<uint32_t>(At<uint8_t>(self, pfield::kDirection1)));
                    if (oneWay)
                        OneWayBits(info,
                                   At<uint32_t>(self, pfield::kDirection1) + (static_cast<uint32_t>(opposite) & 1) * 8,
                                   static_cast<uint32_t>(opposite) * 2);
                }
            }
            char* const word = info + cell::kByType + type * 4;
            CallThis<void>(addr::cSC4NetworkTool_ClearSideHighBits, word);
            CallThis<void, uint32_t*>(addr::cSC4NetworkTool_MergeSideKinds, word, &edges);
            CallThis<void>(addr::cSC4NetworkCellInfo_CombineEdges, info);
        }
    }
}

// 0x006383A0 (ret 4: a network type): for a type whose pen is two cells or
// more wide (its row's +0x1C, unsigned), the sides 3 and 1 (+0x6C set) or
// 2 and 0: over the first min(valid, regular) steps (signed), each cell
// with a cell info gets kind 4 on the first side when it is not its step's
// last cell and on the second when it is not the first (the side's byte's
// bits 2 and up kept, 0x00620FC0), then CombineEdges.
void __fastcall MarkWideSides(char* self, void*, uint32_t type)
{
    static bool logged = false;
    FirstCall("MarkWideSides", logged);
    if (At<uint32_t>(Row(type), prow::kPenWidthX) < 2u)
        return;
    uint32_t sideA, sideB;
    if (At<uint8_t>(self, pfield::kByte6C)) {
        sideA = 3;
        sideB = 1;
    } else {
        sideA = 2;
        sideB = 0;
    }
    const int32_t valid = At<int32_t>(self, field::kValidSteps);
    const int32_t regular = At<int32_t>(self, pfield::kRegularSteps);
    int32_t count = valid < regular ? valid : regular;
    if (count <= 0)
        return;
    for (int32_t i = 0; count != 0; --count, ++i) {
        for (uint32_t c = FirstCell(self, i); c <= LastCell(self, i); ++c) {
            char* const info = Entry(self, CellKey(self, c));
            if (!info)
                continue;
            if (c < LastCell(self, i)) {
                uint32_t* const word = &At<uint32_t>(info, cell::kByType + type * 4);
                const uint32_t value = ((*word >> ((sideA << 3) & 31)) & 0xFFFFFFFCu) | kWideSideKind;
                CallThis<uint32_t*, uint32_t, uint32_t>(addr::cSC4NetworkTool_SetSideByte, word, sideA, value);
            }
            if (c > FirstCell(self, i)) {
                uint32_t* const word = &At<uint32_t>(info, cell::kByType + type * 4);
                const uint32_t value = ((*word >> ((sideB << 3) & 31)) & 0xFFFFFFFCu) | kWideSideKind;
                CallThis<uint32_t*, uint32_t, uint32_t>(addr::cSC4NetworkTool_SetSideByte, word, sideB, value);
            }
            CallThis<void>(addr::cSC4NetworkCellInfo_CombineEdges, info);
        }
    }
}

// 0x006384E0 (no arguments): for each rule cell record (+0xF0, 0x14 bytes,
// the end taken once) inside the city (x and z below its size, unsigned):
// its cell info, made for the record's type when there is none
// (MakeTypeCellInfo; none: skipped); the type's flag (0x00AA8B40) ORed
// into the cell's flags, the type's edge word's high bits cleared and the
// record's edge word merged in, the record's bits 2 of each byte set in
// place of the whole byte; CombineEdges; with a one-way mask (+0x11) the
// one-way byte's masked bits replaced by +0x10 (an 8-bit add).
void __fastcall ApplyRuleCellRecords(char* self, void*)
{
    static bool logged = false;
    FirstCall("ApplyRuleCellRecords", logged);
    char* const end = At<char*>(self, field::kRuleCellRecords + 4);
    for (char* record = VectorAt(self, field::kRuleCellRecords).begin; record != end; record += rcell::kSize) {
        const uint32_t x = At<uint32_t>(record, rcell::kX), z = At<uint32_t>(record, rcell::kZ);
        if (x >= At<uint32_t>(self, field::kCellsX) || z >= At<uint32_t>(self, field::kCellsZ))
            continue;
        char* info = Entry(self, (z << 16) + x);
        if (!info) {
            info = CallThis<char*, uint32_t, uint32_t, uint32_t>(addr::cSC4NetworkTool_MakeTypeCellInfo, self, x, z,
                                                                 At<uint32_t>(record, rcell::kType));
            if (!info)
                continue;
        }
        uint32_t edge = At<uint32_t>(record, rcell::kEdge);
        const uint32_t type = At<uint32_t>(record, rcell::kType);
        uint32_t* const word = &At<uint32_t>(info, cell::kByType + type * 4);
        At<uint32_t>(info, cell::kFlags) =
            At<uint32_t>(info, cell::kFlags) | Global<uint32_t>(addr::cSC4NetworkTool_RuleCellTypeFlags + type * 4);
        CallThis<void>(addr::cSC4NetworkTool_ClearSideHighBits, word);
        CallThis<void, uint32_t*>(addr::cSC4NetworkTool_MergeSideKinds, word, &edge);
        const uint32_t bits = At<uint32_t>(record, rcell::kEdge) & 0x04040404u;
        if (bits)
            *word = (~((bits >> 2) * 0xFFu) & *word) + bits;
        CallThis<void>(addr::cSC4NetworkCellInfo_CombineEdges, info);
        const uint8_t mask = At<uint8_t>(record, rcell::kMask);
        if (mask)
            At<uint8_t>(info, cell::kOneWay) = static_cast<uint8_t>(
                (static_cast<uint8_t>(~mask) & At<uint8_t>(info, cell::kOneWay)) + At<uint8_t>(record, rcell::kBits));
    }
}

// 0x00638770 (ret 4: a tunnel record {start height, end height out, start
// step, end step out}): the end step set to -1, the best difference
// FLT_MAX; from the step after the start while below the valid step count
// less one (re-read after the first), regular (+0x8 0, below +0x74) and dry
// (no cell under water: the terrain's slot 0x88): the step's ground (four
// altitudes: last cell's B, first cell's A, first's B, last's A for the
// opposite direction (+0x7C - 4) & 7, A and B the vertex offsets +0x250 and
// +0x270) below the previous step's (or the start height) makes it a
// candidate, kept when its float is nearer the start height than the best
// (|cur - start| < best: end step and end height); the search ends where
// the first tunnel height plus the start height reaches the ground. No end:
// AL 0. Else tan(+0x2AC) * +0x238 * the span against the best (x87, below)
// below: AL 0; else AL 1 when the span is above 2 (EAX 0 or 1 as the
// game's; its other AL 0 path leaves the record's address above, unread).
uint32_t __fastcall FindTunnelEnd(char* self, void*, uint32_t* record)
{
    static bool logged = false;
    FirstCall("FindTunnelEnd", logged);
    float previous = Float(record[tunnel::kStartHeight / 4]);
    const int32_t direction = (At<int32_t>(self, pfield::kDirection1) - 4) & 7;
    int32_t j = static_cast<int32_t>(record[tunnel::kStartStep / 4]) + 1;
    record[tunnel::kEndStep / 4] = 0xFFFFFFFFu;
    int32_t limit = At<int32_t>(self, field::kValidSteps) - 1;
    float best = Float(cell::kHighestBits);
    void* const terrain = Terrain(self);
    for (; j < limit; ++j, limit = At<int32_t>(self, field::kValidSteps) - 1) {
        char* const step = Steps(self) + j * step::kSize;
        if (At<int32_t>(step, step::kIrregular) != 0 || j >= At<int32_t>(self, pfield::kRegularSteps))
            break;
        bool wet = false;
        for (uint32_t c = FirstCell(self, j); c <= LastCell(self, j); ++c) {
            char* const at = Cells(self) + c * kPointSize;
            if (Virtual<uint8_t>(terrain, pxslot::kTerrainUnderWater, At<uint32_t>(at, 0), At<uint32_t>(at, 4))) {
                wet = true;
                break;
            }
        }
        if (wet)
            break;
        const uint32_t first = VertexOf(self, FirstCell(self, j));
        const uint32_t last = VertexOf(self, LastCell(self, j));
        const uint32_t a = VertexOffset(self, pfield::kVertexOffsetsA, direction);
        const uint32_t b = VertexOffset(self, pfield::kVertexOffsetsB, direction);
        const double product = FourAltitudes(Terrain(self), last + b, first + a, first + b, last + a);
        const float current = static_cast<float>(product);
        const float before = previous;
        previous = current;
        if (product < static_cast<double>(before)) { // FCOMP, `test ah, 5; jp`: below only when ordered
            const double difference =
                std::fabs(static_cast<double>(current) - static_cast<double>(Float(record[tunnel::kStartHeight / 4])));
            if (difference < static_cast<double>(best)) { // FCOM, `test ah, 5; jp`: below only when ordered
                best = static_cast<float>(difference);
                record[tunnel::kEndStep / 4] = static_cast<uint32_t>(j);
                record[tunnel::kEndHeight / 4] = Bits(current);
            }
        }
        const double top = static_cast<double>(At<float>(At<char*>(self, pfield::kTunnelHeights), 0)) +
                           static_cast<double>(Float(record[tunnel::kStartHeight / 4]));
        if (top >= static_cast<double>(current)) // FCOMP, `test ah, 1; je`: not below (a NaN goes on)
            break;
    }
    if (static_cast<int32_t>(record[tunnel::kEndStep / 4]) < 0)
        return 0;
    const int32_t span =
        static_cast<int32_t>(record[tunnel::kEndStep / 4]) - static_cast<int32_t>(record[tunnel::kStartStep / 4]);
    const uint32_t status =
        X87TunnelSlope(&At<float>(self, pfield::kTunnelAngle), &At<float>(self, life::kTerrainScale), &span, &best);
    // `test ah, 5; jp`: on to the span test when the bits C0 and C2 are both
    // clear or both set (above, equal or unordered); AL 0 when only one is.
    const uint32_t c0c2 = (status >> 8) & 5u;
    if (c0c2 == 1u || c0c2 == 4u)
        return 0;
    return span > 2 ? 1u : 0u;
}

// 0x00639490 (ret 4: the step count's address): nothing unless tunnels are
// allowed (slot 62) and the count (read once for the first search) is 4 or
// more; the first regular step (+0x8 0) below the count; its ground (four
// altitudes: first cell's A, first's B, last cell's A, last's B for the
// first direction +0x7C) stored as a float; then for each later step below
// the count (re-read): one that is regular, after two regular ones (one
// when it is the second), and below +0x74 gets its ground (last's B,
// first's A, first's B, last's A), which becomes the previous; when the
// unrounded ground rises above the previous by more than +0x23C (ordered)
// and the step is the third or later, a tunnel search from the step
// before (0x00638770 with the previous ground); a found tunnel of four
// steps or more is appended to +0xE4 (SC4LaneEntryVector::PushBack, 16
// bytes) and its steps get kind 2 at both ends and 3 between.
void __fastcall FindTunnels(char* self, void*, int32_t* count)
{
    static bool logged = false;
    FirstCall("FindTunnels", logged);
    if (!Virtual<uint8_t>(self, pslot::kAllowTunnels))
        return;
    const int32_t n = *count;
    if (n < 4)
        return;
    const int32_t direction = At<int32_t>(self, pfield::kDirection1);
    int32_t i = 0;
    if (n > 0)
        for (char* word = Steps(self) + step::kIrregular; At<uint32_t>(word, 0) != 0;) {
            ++i;
            word += step::kSize;
            if (i >= n)
                break;
        }
    if (i >= n)
        return;
    const uint32_t a = VertexOffset(self, pfield::kVertexOffsetsA, direction);
    const uint32_t b = VertexOffset(self, pfield::kVertexOffsetsB, direction);
    float previous;
    {
        const uint32_t first = VertexOf(self, FirstCell(self, i));
        const uint32_t last = VertexOf(self, LastCell(self, i));
        void* const terrain = Terrain(self);
        previous = static_cast<float>(FourAltitudes(terrain, first + a, first + b, last + a, last + b));
    }
    for (++i; i < *count; ++i) {
        char* const step = Steps(self) + i * step::kSize;
        if (At<uint32_t>(step, step::kIrregular) != 0 || At<uint32_t>(step - step::kSize, step::kIrregular) != 0)
            continue;
        if (i * static_cast<int32_t>(step::kSize) >= 0x18 &&
            At<uint32_t>(step - 2 * step::kSize, step::kIrregular) != 0)
            continue;
        if (i >= At<int32_t>(self, pfield::kRegularSteps))
            continue;
        const uint32_t first = VertexOf(self, FirstCell(self, i));
        const uint32_t last = VertexOf(self, LastCell(self, i));
        const uint32_t va = VertexOffset(self, pfield::kVertexOffsetsA, direction);
        const uint32_t vb = VertexOffset(self, pfield::kVertexOffsetsB, direction);
        const float before = previous;
        const double product = FourAltitudes(Terrain(self), last + vb, first + va, first + vb, last + va);
        previous = static_cast<float>(product);
        // FSUB, FCOMP +0x23C, `test ah, 0x41; jne`: on only when above (ordered).
        if (!(product - static_cast<double>(before) > static_cast<double>(At<float>(self, life::kTerrainTan))))
            continue;
        if (i * static_cast<int32_t>(step::kSize) <= static_cast<int32_t>(step::kSize))
            continue;
        uint32_t record[4] = {Bits(before), 0u, static_cast<uint32_t>(i - 1), 0u};
        if (!static_cast<uint8_t>(CallThis<uint32_t, uint32_t*>(addr::cSC4NetworkTool_FindTunnelEnd, self, record)))
            continue;
        const int32_t from = static_cast<int32_t>(record[2]), to = static_cast<int32_t>(record[3]);
        if (to - from < 4)
            continue;
        CallThis<void, const uint32_t*>(addr::SC4LaneEntryVector_PushBack, self + field::kVectorE4, record);
        At<uint32_t>(Steps(self) + from * step::kSize, step::kIrregular) = 2;
        At<uint32_t>(Steps(self) + to * step::kSize, step::kIrregular) = 2;
        for (int32_t k = from + 1; k < to; ++k)
            At<uint32_t>(Steps(self) + k * step::kSize, step::kIrregular) = 3;
    }
}

// ====================================================================================
// The intersections' template code
// ====================================================================================

// Copies an intersection's seven words (+0x0 to +0x18).
inline void CopyWords(char* to, const char* from, std::size_t words)
{
    for (std::size_t w = 0; w < words; ++w)
        At<uint32_t>(to, w * 4) = At<uint32_t>(const_cast<char*>(from), w * 4);
}

inline void CopyConstructCells(char* to, const char* from)
{
    CallThis<void, const char*>(addr::SC4Uint32Vector_CopyConstruct, to + isect::kCells, from + isect::kCells);
}

// 0x006251F0 (ret 4: the source; this a new uint32 vector): cleared,
// memory for the source's count (signed shift; none for 0), the words
// copied (the count taken again); EAX this.
Vector* __fastcall CopyConstruct(Vector* self, void*, const Vector* source)
{
    static bool logged = false;
    FirstCall("CopyConstruct", logged);
    const int32_t count = static_cast<int32_t>(source->end - source->begin) >> 2;
    self->begin = nullptr;
    self->end = nullptr;
    self->capacity = nullptr;
    char* const memory = count ? CallCdecl<char*, uint32_t>(addr::SC4_stl_allocate, static_cast<uint32_t>(count) * 4)
                               : nullptr;
    self->begin = memory;
    self->end = memory;
    self->capacity = memory + count * 4;
    char* to = memory;
    const int32_t again = static_cast<int32_t>(source->end - source->begin) >> 2;
    const char* from = source->begin;
    for (int32_t k = 0; k < again; ++k, from += 4, to += 4)
        At<uint32_t>(to, 0) = At<uint32_t>(const_cast<char*>(from), 0);
    self->end = to;
    return self;
}

// 0x00627160 (cdecl: first, last, destination; a fourth pushed and not
// read): each intersection copy-constructed where the destination is not
// null (the destination moves on all the same); EAX the end.
char* __cdecl UninitializedCopy(const char* first, const char* last, char* to)
{
    static bool logged = false;
    FirstCall("UninitializedCopy", logged);
    for (; first != last; first += isect::kSize, to += isect::kSize)
        if (to) {
            CopyWords(to, first, kIsectWords);
            CopyConstructCells(to, first);
        }
    return to;
}

// 0x006271C0 (cdecl: destination, count, the value; a fourth pushed and
// not read): count copies (unsigned; none for 0) constructed as above;
// EAX the end.
char* __cdecl UninitializedFill(char* to, uint32_t count, const char* value)
{
    static bool logged = false;
    FirstCall("UninitializedFill", logged);
    for (; count != 0; --count, to += isect::kSize)
        if (to) {
            CopyWords(to, value, kIsectWords);
            CopyConstructCells(to, value);
        }
    return to;
}

// 0x00627220 (cdecl: first, last, destination; two more pushed and not
// read): the count (signed division by 0x28); none or fewer: the
// destination; else each intersection assigned (seven words, the vector by
// SC4IntVector::Assign); EAX the end.
char* __cdecl CopyRange(const char* first, const char* last, char* to)
{
    static bool logged = false;
    FirstCall("CopyRange", logged);
    int32_t count = static_cast<int32_t>(last - first) / static_cast<int32_t>(isect::kSize);
    if (count <= 0)
        return to;
    for (; count != 0; --count, first += isect::kSize, to += isect::kSize) {
        CopyWords(to, first, kIsectWords);
        CallThis<void, const char*>(addr::SC4IntVector_Assign, to + isect::kCells, first + isect::kCells);
    }
    return to;
}

// 0x0062BE80 (ret 8: first, last; this a vector of intersections): the
// tail from last moved down to first (0x00627220 with the first argument's
// slot as the unread tag and 0), the elements left over past the new end
// have their vectors freed, the end set; EAX first.
char* __fastcall Erase(Vector* self, void*, char* first, char* last)
{
    static bool logged = false;
    FirstCall("Erase", logged);
    char* tag = first;
    char* const kept = CallCdecl<char*, char*, char*, char*, void*, uint32_t>(addr::SC4IntersectionVector_CopyRange,
                                                                            last, self->end, first, &tag, 0u);
    char* const end = self->end;
    for (char* at = kept; at != end; at += isect::kSize)
        if (void* const cells = At<void*>(at, isect::kCells))
            CallCdecl<void, void*>(addr::SC4_Free, cells);
    self->end = kept;
    return first;
}

// 0x0062BFE0 (ret 0x14: where, the value, an unread tag, a count, at end;
// this a vector of intersections): new memory for max(size, count) + size
// elements (unsigned; none for 0), the elements before where
// copy-constructed into it, the value once (in place, unless the memory is
// null) or count times, the rest unless at end, the old elements destroyed
// (DestroyIntersections 0x0062BE40), the vector set to the new memory.
void __fastcall InsertOverflow(Vector* self, void*, char* where, const char* value, void*, uint32_t count,
                               uint32_t atEnd)
{
    static bool logged = false;
    FirstCall("InsertOverflow", logged);
    const uint32_t size =
        static_cast<uint32_t>(static_cast<int32_t>(self->end - self->begin) / static_cast<int32_t>(isect::kSize));
    const uint32_t grow = size < count ? count : size;
    const uint32_t capacity = grow + size;
    char* const memory =
        capacity ? CallCdecl<char*, uint32_t>(addr::SC4_stl_allocate, capacity * isect::kSize) : nullptr;
    uint32_t tag = atEnd; // the game hands its fifth argument's slot as the unread tag
    char* to = CallCdecl<char*, char*, char*, char*, void*>(addr::SC4IntersectionVector_UninitializedCopy,
                                                            self->begin, where, memory, &tag);
    if (count == 1) {
        if (to) {
            CopyWords(to, value, kIsectWords);
            CopyConstructCells(to, value);
        }
        to += isect::kSize;
    } else {
        to = CallCdecl<char*, char*, uint32_t, const char*, void*>(addr::SC4IntersectionVector_UninitializedFill, to,
                                                                   count, value, &tag);
    }
    if (!static_cast<uint8_t>(atEnd))
        to = CallCdecl<char*, char*, char*, char*, void*>(addr::SC4IntersectionVector_UninitializedCopy, where,
                                                          self->end, to, &tag);
    CallThis<void>(addr::cSC4NetworkTool_DestroyIntersections, self);
    self->begin = memory;
    self->end = to;
    self->capacity = memory + capacity * isect::kSize;
}

// 0x0062C0F0 (ret 4: the value; this a vector of intersections): the value
// copy-constructed at the end (nothing written at a null end, the end
// moved all the same), or InsertOverflow(end, value, tag, 1, 1) when full.
void __fastcall PushBack(Vector* self, void*, const char* value)
{
    static bool logged = false;
    FirstCall("PushBack", logged);
    char* const end = self->end;
    if (end != self->capacity) {
        if (end) {
            CopyWords(end, value, kIsectWords);
            CopyConstructCells(end, value);
        }
        self->end += isect::kSize;
        return;
    }
    const char* tag = value;
    CallThis<void, char*, const char*, void*, uint32_t, uint32_t>(addr::SC4IntersectionVector_InsertOverflow, self, end,
                                                                  value, &tag, 1u, 1u);
}

// 0x0062C510 (cdecl: first, last, destination; two more pushed and not
// read): the count (signed division by 0x28); none or fewer: the
// destination; else each major bridge assigned (six words, the byte +0x18,
// the cell vector by SC4BridgeCellVector::Assign 0x0062C1E0); EAX the end.
char* __cdecl MajorBridgeCopyRange(const char* first, const char* last, char* to)
{
    static bool logged = false;
    FirstCall("MajorBridgeCopyRange", logged);
    int32_t count = static_cast<int32_t>(last - first) / static_cast<int32_t>(mbridge::kSize);
    if (count <= 0)
        return to;
    for (; count != 0; --count, first += mbridge::kSize, to += mbridge::kSize) {
        CopyWords(to, first, kBridgeWords);
        At<uint8_t>(to, mbridge::kByte18) = At<uint8_t>(const_cast<char*>(first), mbridge::kByte18);
        CallThis<void, const char*>(addr::SC4BridgeCellVector_Assign, to + mbridge::kCells, first + mbridge::kCells);
    }
    return to;
}

// ====================================================================================
// Installation
// ====================================================================================

#define OPENSC4_DETOUR(address, fn) OPENSC4_NETTOOL_DETOUR(address, fn)

const Detour kPlaceCells[] = {
    OPENSC4_DETOUR(addr::cSC4NetworkToolPreview_UpdateCellHeights, UpdateCellHeights),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_PreviewChangedCellHeights, PreviewChangedCellHeights),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_ResolveChangedCells, ResolveChangedCells),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_ResolveChangedVertices, ResolveChangedVertices),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_SetStepEdges, SetStepEdges),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_MarkWideSides, MarkWideSides),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_ApplyRuleCellRecords, ApplyRuleCellRecords),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_FindTunnelEnd, FindTunnelEnd),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_FindTunnels, FindTunnels),
};

const Detour kEdgeWords[] = {
    OPENSC4_DETOUR(addr::cSC4NetworkTool_MergeSideKinds, MergeSideKinds),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_SetSideByte, SetSideByte),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_SetSideFromDirection, SetSideFromDirection),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_ClearSideHighBits, ClearSideHighBits),
};

const Detour kIsectVector[] = {
    OPENSC4_DETOUR(addr::SC4Uint32Vector_CopyConstruct, CopyConstruct),
    OPENSC4_DETOUR(addr::SC4IntersectionVector_UninitializedCopy, UninitializedCopy),
    OPENSC4_DETOUR(addr::SC4IntersectionVector_UninitializedFill, UninitializedFill),
    OPENSC4_DETOUR(addr::SC4IntersectionVector_CopyRange, CopyRange),
    OPENSC4_DETOUR(addr::SC4IntersectionVector_Erase, Erase),
    OPENSC4_DETOUR(addr::SC4IntersectionVector_InsertOverflow, InsertOverflow),
    OPENSC4_DETOUR(addr::SC4IntersectionVector_PushBack, PushBack),
    OPENSC4_DETOUR(addr::SC4MajorBridgeVector_CopyRange, MajorBridgeCopyRange),
};

#undef OPENSC4_DETOUR

bool InstallPlaceCells(registry::Mode) { return InstallDetours(kPlaceCells, "placecells"); }
bool InstallEdgeWords(registry::Mode) { return InstallDetours(kEdgeWords, "edgewords"); }
bool InstallIsectVector(registry::Mode) { return InstallDetours(kIsectVector, "isectvector"); }

const bool g_registered[] = {
    registry::Register({"simulators.nettool.placecells", "simulators/nettool", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4NetworkTool_SetStepEdges,
                        "Our network tool's cell passes while placing: the step edges, the wide sides, the rule cell "
                        "records, the tunnels' search, the changed cells' and vertices' heights and solved cells, the "
                        "preview's height update (9 detours; src/core/nettool_place_2.cpp)",
                        false, InstallPlaceCells}),
    registry::Register({"simulators.nettool.edgewords", "simulators/nettool", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4NetworkTool_SetSideByte,
                        "Our network tool's edge word helpers: MergeSideKinds, SetSideByte, SetSideFromDirection and "
                        "ClearSideHighBits (4 detours; src/core/nettool_place_2.cpp)",
                        false, InstallEdgeWords}),
    registry::Register({"simulators.nettool.isectvector", "simulators/nettool", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::SC4IntersectionVector_InsertOverflow,
                        "Our network tool's intersection vectors' template code (copy, fill, assignment, erase, "
                        "insert, push_back), the uint32 vector's copy constructor and the major bridges' assignment "
                        "copy (8 detours; src/core/nettool_place_2.cpp)",
                        false, InstallIsectVector}),
};

} // namespace
} // namespace opensc4::nettool_sim
