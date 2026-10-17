// Our cSC4NetworkTool family's power line tool, second part (build 641;
// part w1 of docs/nettool.md, second half): the pole records' lookups and
// links, a pole's position, the occupant lookups (the pole and the wire at a
// cell, what stands in a cell), the pole maker 0x00650140, the records'
// clear 0x006505D0, slot 53 DoNetworkSpecificPlacement and slot 10
// UpdateOnZoneChange. The same group as the first part,
// simulators.nettool.power (src/core/nettool_power.cpp registers it; its
// install calls InstallPower2 for this list). Calls between the part's
// functions go to the game's addresses.
//
// Left as the game's:
//   - 0x00650760 (the record append, ret 0x10): the 0x30-byte record it
//     builds on its stack and copies into the vector whole (0x0064F710 or
//     the growing insert) has its byte +0x14 never written: a stack
//     leftover the record keeps, which C++ cannot repeat.
//   - 0x0064FED0 (slot 81, ret 4): the kind of the previous cell it tests
//     for a kind-3 cell of the last step is a local that is not written
//     before the first cell, so a one-step drag whose first cell is of kind
//     3 reads a stack leftover.
//   - 0x006520C0 (ret 0x28, slot 10's per-wire walk): the record it hands
//     to the pole maker 0x00650140 on its stack leaves the byte +0x14
//     unwritten when the cell already has a pole and no pole exemplar is
//     found, and then tests it.
//   - 0x00650840 (3,345 bytes, 36 x87 sites, the pole planner) and
//     0x00651560 (2,903 bytes, 16 x87 sites, the pole placer): larger than
//     this part's time box allows to check line by line; they call this
//     part's functions at their addresses.
//
// NAM. None of these functions is guarded, patched or called by NAM's DLL;
// the tool's records, lists and the occupants stay in the game's memory in
// the game's layout.
//
// Floats (0x0064EFE0 only). The terrain's cell size (slot 0x18, ST0: the
// game's `fld dword; ret`, a float), the cell's x and z as FILD of an int32
// (exact in a double) plus the float 0.5 (exact), each times the size as
// one double product at 53-bit precision, stored as floats (fstp dword:
// the same double rounding as C++'s double product cast to float). The size
// is popped before the next call, so nothing stays on the x87 stack across
// a call. The height: with the terrain's slot 0x88 (x, z) set, the
// terrain's slot 0x78 (the two stored floats; ST0 as a double, exact at
// 53-bit precision) plus 0.5, stored as a float; else the highest of the
// cell's four corner heights (slot 0x44) starting from -FLT_MAX: the first
// corner counts only when above -FLT_MAX (`fcomp`, `test ah,0x41; jne`:
// equal, below and unordered keep -FLT_MAX), each later one only when above
// the highest so far (the same test: NaN never wins), plus 0.5 as a float.
//
// Registers: the direct callers (the game's 0x00650760, 0x00650840,
// 0x00651560, 0x006520C0, 0x0064FED0, and this part's) were scanned
// (scratch regscan.py / eaxscan.py): none reads ECX or EDX after the call
// (two flagged sites, 0x006509CA `xor dl, dl` and 0x00651FCE after `imul`,
// write EDX first). EAX is read after 0x0064F170, 0x0064F1D0, 0x0064FBF0,
// 0x0064FFE0 (AL), 0x0064F230 and 0x0064FD00 (EAX), as given here. Slots
// 10 and 53 keep the general tool's conventions (slot 10's answer is not
// read: the general one is three bytes and leaves EAX).
//
// Calls, each count from the asm's pushes and the callee's own `ret N`:
// the terrain (+0x44) slot 0x18 none (ST0), 0x88 two (AL), 0x78 two floats
// (ST0), 0x44 three (x, z, four floats out); the list node allocator
// 0x00B207E4 slot 0xC one and slot 0x10 one; SC4List::Clear2 0x00413E60 and
// Destroy2 0x00413FE0 none; the occupant manager (0x00B43D0C or the tool's
// +0x48) slot 0xA0 five (list, low cell, high cell, 0, -1), slot 0x4C four
// (two position floats, x out, z out), slot 0x54 three, slot 0x50 two; an
// occupant's QueryInterface (slot 0) two, slot 0x1C none, slot 0x20 one (a
// position out), slot 8 none; the zone manager 0x00B43D14 slot 0x30 two;
// the lot manager 0x00B43D08 slot 0x18 three; SC4PointVector::InsertN
// 0x007C61A0 five (ret 0x14); 0x0061F4C0 cdecl four (the direction from a
// cell to another; AL); GetPieceExemplar 0x006244B0 two (ret 8);
// cSC4PowerPoleOccupant::Allocate 0x006499D0 cdecl one (0x124) and its
// constructor 0x0064DB30 one (1); a pole's slots 4, 8, 0x3C two, 0x38 one,
// its +8 part's slot 0x24 one, its +0x24 part's slot 0xC one; a wire's
// slots 0xC three (two poles out, a third out), 0x14 two, 0x10 none, 0x18
// one, 0x1C one, 0x40 two; SC4Network::ConnectEnds 0x0064DA00 two (ret 8);
// AddEdgeConnection 0x00628BD0 two (ret 8), ApplyNeighborConnections
// 0x00624610 none; the tool's slot 82 (+0x148) one; the STL allocator
// 0x0090CF54 and the free 0x0090CF63 cdecl one; this part's 0x0064EFE0
// three (ret 0xC), 0x0064FFE0 cdecl three, 0x006502F0 two, 0x00650360 two,
// 0x006505D0 none, 0x00650840 none, 0x00651560 one (ret 4), 0x006520C0 ten
// (ret 0x28).
#include "core/nettool_power.h"
#include "core/nettool_sim.h"

#include "core/game.h"
#include "core/log.h"

#include <opensc4/addresses.h>

#include <bit>
#include <cstddef>
#include <cstdint>

namespace opensc4::nettool_sim {
namespace {

// A node of the game's lists (the occupant manager's answers, the lists
// slot 10 keeps): next, previous, a value.
struct Node {
    Node* next;
    Node* previous;
    void* value;
};

inline constexpr uint32_t kFltMaxNegBits = 0xFF7FFFFF;
inline constexpr std::size_t kAllocate = 0xC, kDeallocate = 0x10;

// The IIDs and occupant types the lookups ask for.
inline constexpr uint32_t kIidPowerPole = 0xA8C86142, kIidWire = 0xC98F3B24, kIidPipeBase = 0x4A10AFC2;
inline constexpr uint32_t kTypeFlora = 0x278128A0, kTypeNetwork = 0xC772BF98;
// The occupant manager's slots.
inline constexpr std::size_t kByCityCells = 0xA0, kCellOfPosition = 0x4C, kRemoveHighlight = 0x54;
// A pole occupant's: the wire sides it has (+0xB8).
inline constexpr std::size_t kPoleSides = 0xB8;

char* Alloc(uint32_t bytes) { return CallCdecl<char*, uint32_t>(addr::SC4_stl_allocate, bytes); }
void Free(void* p) { CallCdecl<void, void*>(addr::SC4_Free, p); }
Vector& VectorAt(char* object, std::size_t offset) { return At<Vector>(object, offset); }
void* Terrain(char* self) { return At<void*>(self, field::kTerrain); }

// A new list of the list node allocator (head linked to itself).
Node* NewList()
{
    Node* const head = Virtual<Node*>(ListNodeAllocator(), kAllocate, uint32_t{0xC});
    head->next = head;
    head->previous = head;
    return head;
}

// The occupants of one cell as the lookups ask the occupant manager: the
// cell as both corners, no filter, no limit.
void OccupantsAt(void* manager, Node** list, uint32_t x, uint32_t z)
{
    uint32_t cell[2] = {x, z};
    Virtual<void>(manager, kByCityCells, list, cell, cell, 0u, 0xFFFFFFFFu);
}

// A node of the STL allocator with the value appended to a list (the game
// tests the node's +8 for null before the value's store, which only a null
// node fails, and then faults on the links).
void PushBack(Node* head, void* value)
{
    Node* const node = reinterpret_cast<Node*>(Alloc(0xC));
    node->value = value;
    Node* const last = head->previous;
    node->next = head;
    node->previous = last;
    last->next = node;
    head->previous = node;
}

// ====================================================================================
// The pole records
// ====================================================================================

// 0x0064EFE0 (ret 0xC: x, z, three floats out): a pole's position in the
// cell's middle (see Floats in the file comment).
void __fastcall PolePosition(char* self, void*, int32_t x, int32_t z, float* out)
{
    static bool logged = false;
    FirstCall("PowerLinePolePosition", logged);
    const double size = static_cast<double>(Virtual<float>(Terrain(self), 0x18));
    out[0] = static_cast<float>((static_cast<double>(x) + 0.5) * size);
    out[2] = static_cast<float>((static_cast<double>(z) + 0.5) * size);
    if (Virtual<uint8_t>(Terrain(self), 0x88, x, z)) {
        const double height = Virtual<double>(Terrain(self), 0x78, out[0], out[2]);
        out[1] = static_cast<float>(height + 0.5);
        return;
    }
    float corners[4];
    Virtual<void>(Terrain(self), 0x44, x, z, corners);
    const float lowest = std::bit_cast<float>(kFltMaxNegBits);
    double highest = corners[0] > lowest ? corners[0] : lowest;
    for (int i = 1; i < 4; ++i)
        if (static_cast<double>(corners[i]) > highest)
            highest = corners[i];
    out[1] = static_cast<float>(highest + 0.5);
}

// 0x0064F170 (ret 4: a key of two words and two bytes): AL 1 when a pole
// record's words +0x24, +0x28 and bytes +0x2C, +0x2D equal the key's.
uint8_t __fastcall HasPoleRecordWithKey(char* self, void*, const char* key)
{
    static bool logged = false;
    FirstCall("PowerLineHasPoleRecordWithKey", logged);
    char* const k = const_cast<char*>(key);
    for (char* r = VectorAt(self, power::kPoles).begin; r != VectorAt(self, power::kPoles).end; r += pole::kSize)
        if (At<uint32_t>(r, pole::kObject24) == At<uint32_t>(k, 0) &&
            At<uint32_t>(r, pole::kObject28) == At<uint32_t>(k, 4) &&
            At<uint8_t>(r, pole::kWord2C) == At<uint8_t>(k, 8) &&
            At<uint8_t>(r, pole::kWord2C + 1) == At<uint8_t>(k, 9))
            return 1;
    return 0;
}

// 0x0064F1D0 (ret 4: an occupant): AL 0 without one; else its cell (slot
// 0x20: x out, z out) and AL 1 when a pole record has that x or that z.
uint8_t __fastcall HasPoleInLine(char* self, void*, void* occupant)
{
    static bool logged = false;
    FirstCall("PowerLineHasPoleInLine", logged);
    if (!occupant)
        return 0;
    uint32_t x = 0;
    uint32_t z = reinterpret_cast<std::uintptr_t>(occupant); // the game's out is its argument slot
    Virtual<void>(occupant, 0x20, &x, &z);
    char* const end = VectorAt(self, power::kPoles).end;
    for (char* r = VectorAt(self, power::kPoles).begin; r != end; r += pole::kSize)
        if (At<uint32_t>(r, pole::kX) == x || At<uint32_t>(r, pole::kZ) == z)
            return 1;
    return 0;
}

// 0x0064F230 (ret 8: a step, an anchor point's index): the step's first
// cell when +0x74 is set and the step is below it (signed); else the first
// dragged cell of the step (first to last, signed) whose distance from the
// anchor point is the same along x and z (absolute values of the 32-bit
// differences), or -1.
int32_t __fastcall FindDiagonalCell(char* self, void*, int32_t s, int32_t anchor)
{
    static bool logged = false;
    FirstCall("PowerLineFindDiagonalCell", logged);
    const char* const st = VectorAt(self, field::kSteps).begin + s * static_cast<int32_t>(step::kSize);
    const int32_t first = At<int32_t>(const_cast<char*>(st), 0);
    const int32_t last = At<int32_t>(const_cast<char*>(st), step::kLastCell);
    const int32_t limit = At<int32_t>(self, 0x74);
    if (limit != 0 && s < limit)
        return first;
    char* const point = VectorAt(self, field::kAnchors).begin + anchor * static_cast<int32_t>(kPointSize);
    const int32_t ax = At<int32_t>(point, 0), az = At<int32_t>(point, 4);
    for (int32_t i = first; i <= last; ++i) {
        char* const cell = VectorAt(self, field::kCells).begin + i * static_cast<int32_t>(kPointSize);
        const uint32_t dz = static_cast<uint32_t>(az - At<int32_t>(cell, 4));
        const uint32_t dx = static_cast<uint32_t>(ax - At<int32_t>(cell, 0));
        const uint32_t adz = static_cast<int32_t>(dz) < 0 ? 0u - dz : dz;
        const uint32_t adx = static_cast<int32_t>(dx) < 0 ? 0u - dx : dx;
        if (adx == adz)
            return i;
    }
    return -1;
}

// One link appended to a record's vector (+0x18): the pair (an index, the
// word) at the end, or through SC4PointVector::InsertN (end, the pair, the
// address of the word's argument slot, 1, 1) when it is full.
void AppendLink(char* record, uint32_t* pair, uint32_t* word)
{
    char* const vector = record + pole::kLinks;
    char* const end = At<char*>(record, pole::kLinksEnd);
    if (end != At<char*>(record, pole::kLinksCap)) {
        if (end) {
            At<uint32_t>(end, 0) = pair[0];
            At<uint32_t>(end, 4) = pair[1];
        }
        At<char*>(record, pole::kLinksEnd) += 8;
        return;
    }
    CallThis<void, char*, uint32_t*, uint32_t*, uint32_t, uint32_t>(addr::SC4PointVector_InsertN, vector, end, pair,
                                                                    word, 1u, 1u);
}

// 0x0064FB40 (ret 0xC: two pole records, a word): each gets a link to the
// other ((the other's index, the word), the first's vector first), then the
// direction from the first's cell to the second's (0x0061F4C0) as a bit
// (1 << it, a byte: none for 8 and up) ORed into both records' sides byte.
void __fastcall LinkPoles(char* self, void*, char* a, char* b, uint32_t word)
{
    static bool logged = false;
    FirstCall("PowerLineLinkPoles", logged);
    (void)self;
    uint32_t pair[2] = {At<uint32_t>(b, pole::kIndex), word};
    AppendLink(a, pair, &word);
    pair[0] = At<uint32_t>(a, pole::kIndex);
    AppendLink(b, pair, &word);
    const uint8_t direction = CallCdecl<uint8_t, uint32_t, uint32_t, uint32_t, uint32_t>(
        addr::SC4_DirectionBetweenCells, At<uint32_t>(a, pole::kX), At<uint32_t>(a, pole::kZ),
        At<uint32_t>(b, pole::kX), At<uint32_t>(b, pole::kZ));
    const uint32_t count = direction & 31u;
    const uint8_t bit = count < 8 ? static_cast<uint8_t>(1u << count) : uint8_t{0};
    At<uint8_t>(a, pole::kSides) |= bit;
    At<uint8_t>(b, pole::kSides) |= bit;
}

// 0x006505D0 (no arguments): every pole record's objects +0x24 and +0x28
// released (when set) and cleared (the end read once), then all records
// erased (0x006502F0 with begin and end).
void __fastcall ClearPoleRecords(char* self, void*)
{
    static bool logged = false;
    FirstCall("PowerLineClearPoleRecords", logged);
    char* const end = VectorAt(self, power::kPoles).end;
    for (char* r = VectorAt(self, power::kPoles).begin; r != end; r += pole::kSize) {
        if (void* const first = At<void*>(r, pole::kObject24)) {
            ReleaseOf(first);
            At<void*>(r, pole::kObject24) = nullptr;
        }
        if (void* const second = At<void*>(r, pole::kObject28)) {
            ReleaseOf(second);
            At<void*>(r, pole::kObject28) = nullptr;
        }
    }
    CallThis<char*, char*, char*>(addr::SC4PowerPoleRecordVector_Erase, self + power::kPoles,
                                  VectorAt(self, power::kPoles).begin, VectorAt(self, power::kPoles).end);
}

// ====================================================================================
// The occupant lookups and the pole maker
// ====================================================================================

// 0x0064FBF0 (ret 0xC: x, z, an out pointer): a new list (cleared, Clear2),
// the occupants of the cell (the occupant manager 0x00B43D0C); the out
// cleared; each one asked for the wire interface 0xC98F3B24 (into the
// game's x argument slot, cleared first): found, the out's old one
// released and the out gets it; each occupant released. The list
// destroyed (Destroy2). AL found.
uint8_t __fastcall FindWireAt(char* self, void*, uint32_t x, uint32_t z, void** out)
{
    static bool logged = false;
    FirstCall("PowerLineFindWireAt", logged);
    (void)self;
    uint8_t found = 0;
    Node* list = NewList();
    CallThis<void>(addr::SC4List_Clear2, &list);
    OccupantsAt(Global<void*>(addr::SC4_NetworkService), &list, x, z);
    Node* const head = list;
    *out = nullptr;
    for (Node* node = head->next; node != head; node = node->next) {
        void* const occupant = node->value;
        void* wire = nullptr;
        if (Virtual<uint8_t>(occupant, 0x0, kIidWire, &wire)) {
            if (*out)
                ReleaseOf(*out);
            *out = wire;
            found = 1;
        }
        ReleaseOf(occupant);
    }
    CallThis<void>(addr::SC4List_Destroy2, &list);
    return found;
}

// 0x0064FD00 (ret 8: x, z): what stands in the cell, 0 for nothing:
//   - a new list; the zone manager's slot 0x30 at (x, z) not 0, at (x, z)
//     not 0xF and at (z, z) (the game's) not 0xE: 1;
//   - else the occupants of the cell (the tool's occupant manager +0x48):
//     flora (type 0x278128A0) 2; a network occupant (0xC772BF98) without
//     the pipe base interface 0x4A10AFC2 3 (with it: the interface
//     released); each released; the list cleared (Clear2); when still 0,
//     the lot manager's slot 0x18 (x, z, 0) not 0: 2;
//   - the list cleared again (Clear2) and its head given back (allocator
//     slot 0x10). EAX the kind.
uint32_t __fastcall ClassifyCell(char* self, void*, uint32_t x, uint32_t z)
{
    static bool logged = false;
    FirstCall("PowerLineClassifyCell", logged);
    Node* list = NewList();
    uint32_t kind = 0;
    void* const zones = Global<void*>(addr::SC4_ZoneManager);
    if (Virtual<int32_t>(zones, 0x30, x, z) != 0 && Virtual<int32_t>(zones, 0x30, x, z) != 0xF &&
        Virtual<int32_t>(zones, 0x30, z, z) != 0xE) {
        kind = 1;
    } else {
        OccupantsAt(At<void*>(self, field::kOccupants), &list, x, z);
        Node* const head = list;
        for (Node* node = head->next; node != head; node = node->next) {
            void* const occupant = node->value;
            const uint32_t type = Virtual<uint32_t>(occupant, 0x1C);
            if (type == kTypeFlora) {
                kind = 2;
            } else if (type == kTypeNetwork) {
                void* base = reinterpret_cast<void*>(static_cast<std::uintptr_t>(z)); // the game's z slot
                if (Virtual<uint8_t>(occupant, 0x0, kIidPipeBase, &base))
                    ReleaseOf(base);
                else
                    kind = 3;
            }
            ReleaseOf(occupant);
        }
        CallThis<void>(addr::SC4List_Clear2, &list);
        if (kind == 0 && Virtual<int32_t>(Global<void*>(addr::SC4_LotManager), 0x18, x, z, 0u) != 0)
            kind = 2;
    }
    CallThis<void>(addr::SC4List_Clear2, &list);
    Node* const sentinel = list;
    Virtual<void>(ListNodeAllocator(), kDeallocate, static_cast<void*>(sentinel));
    return kind;
}

// 0x0064FFE0 (cdecl: x, z, an out pointer): a new list (cleared), the
// occupants of the cell (the occupant manager 0x00B43D0C); the out cleared;
// each one asked for the power pole interface 0xA8C86142 (into the game's
// out argument slot, cleared first): found, its position (slot 0x20) to a
// cell (the occupant manager's slot 0x4C: the position's x and z words);
// that cell the asked one: the out's old one released, the out gets it,
// AL 1 later; else the interface released. Each occupant released; the
// list destroyed (Destroy2). AL found.
uint8_t __cdecl FindPoleAt(uint32_t x, uint32_t z, void** out)
{
    static bool logged = false;
    FirstCall("PowerLineFindPoleAt", logged);
    uint8_t found = 0;
    Node* list = NewList();
    CallThis<void>(addr::SC4List_Clear2, &list);
    OccupantsAt(Global<void*>(addr::SC4_NetworkService), &list, x, z);
    Node* const head = list;
    *out = nullptr;
    for (Node* node = head->next; node != head; node = node->next) {
        void* const occupant = node->value;
        void* pole = nullptr;
        if (Virtual<uint8_t>(occupant, 0x0, kIidPowerPole, &pole)) {
            uint32_t position[3] = {0, 0, 0};
            Virtual<void>(occupant, 0x20, position);
            uint32_t cx = 0, cz = 0;
            Virtual<void>(Global<void*>(addr::SC4_NetworkService), kCellOfPosition, position[0], position[2], &cx,
                          &cz);
            if (cx == x && cz == z) {
                if (*out)
                    ReleaseOf(*out);
                *out = pole;
                found = 1;
            } else {
                ReleaseOf(pole);
            }
        }
        ReleaseOf(occupant);
    }
    CallThis<void>(addr::SC4List_Destroy2, &list);
    return found;
}

// 0x00650140 (ret 4: a pole record): the sides bits (+0x15); the pole at
// the record's cell (0x0064FFE0; out in the game's argument slot):
//   - found: the record's occupant (+0x10) is it, the sides ORed with the
//     pole's own (+0xB8); the exemplar of the sides (the table 0x00B467B0)
//     looked up (GetPieceExemplar: the ID, 0); found, the pole's +0x24 part
//     slot 0xC with it, the record's byte +0x14 cleared, the exemplar
//     released (without one +0x14 is left as it was);
//   - else a new pole (Allocate 0x124, the constructor with 1 when there is
//     memory, else null; AddRef'd), the record's occupant, +0x14 set; the
//     exemplar of the sides; found, the pole's slot 0x3C (it, the sides)
//     and the exemplar released;
//   - the pole's +0xB8 gets the sides; its position (0x0064EFE0); when the
//     record has an occupant, its +8 part's slot 0x24 with the position and
//     its slot 0x38 with "the tool's +0x50 is clear".
void __fastcall MakePole(char* self, void*, char* record)
{
    static bool logged = false;
    FirstCall("PowerLineMakePole", logged);
    uint32_t sides = At<uint8_t>(record, pole::kSides);
    void* found = record; // the game's out is its argument slot
    if (CallCdecl<uint8_t, uint32_t, uint32_t, void**>(addr::cSC4PowerLineTool_FindPoleAt,
                                                       At<uint32_t>(record, pole::kX), At<uint32_t>(record, pole::kZ),
                                                       &found)) {
        char* const existing = static_cast<char*>(found);
        At<char*>(record, pole::kOccupant) = existing;
        sides |= At<uint32_t>(existing, kPoleSides);
        const uint32_t id = Global<uint32_t>(power::kPoleExemplars + sides * 4);
        if (void* const exemplar = CallThis<void*, uint32_t, uint32_t>(addr::cSC4NetworkTool_GetPieceExemplar, self,
                                                                       id, 0u)) {
            char* const occupant = At<char*>(record, pole::kOccupant);
            Virtual<void>(occupant + 0x24, 0xC, exemplar);
            At<uint8_t>(record, pole::kNew) = 0;
            ReleaseOf(exemplar);
        }
    } else {
        char* occupant = nullptr;
        if (char* const memory = CallCdecl<char*, uint32_t>(addr::cSC4PowerPoleOccupant_Allocate, 0x124u))
            occupant = CallThis<char*, uint32_t>(addr::cSC4PowerPoleOccupant_cSC4PowerPoleOccupant, memory, 1u);
        At<char*>(record, pole::kOccupant) = occupant;
        AddRefOf(occupant);
        At<uint8_t>(record, pole::kNew) = 1;
        const uint32_t id = Global<uint32_t>(power::kPoleExemplars + sides * 4);
        if (void* const exemplar = CallThis<void*, uint32_t, uint32_t>(addr::cSC4NetworkTool_GetPieceExemplar, self,
                                                                       id, 0u)) {
            Virtual<void>(At<void*>(record, pole::kOccupant), 0x3C, exemplar, sides);
            ReleaseOf(exemplar);
        }
    }
    At<uint32_t>(At<char*>(record, pole::kOccupant), kPoleSides) = sides;
    float position[3];
    CallThis<void, uint32_t, uint32_t, float*>(addr::cSC4PowerLineTool_PolePosition, self,
                                               At<uint32_t>(record, pole::kX), At<uint32_t>(record, pole::kZ),
                                               position);
    if (char* const occupant = At<char*>(record, pole::kOccupant)) {
        Virtual<void>(occupant + 8, 0x24, position);
        const uint8_t hidden = At<uint8_t>(self, field::kReplacing) == 0 ? 1 : 0;
        Virtual<void>(At<char*>(record, pole::kOccupant), 0x38, hidden);
    }
}

// ====================================================================================
// Slots 53 and 10
// ====================================================================================

// 0x00652980 slot 53 DoNetworkSpecificPlacement (ret 8: a word for
// AddEdgeConnection, a byte "place"): +0x384 and +0x388 cleared, the pole
// records cleared (0x006505D0), the vectors +0x378 erased (0x00650360); the
// valid step count (+0x78) = the step count (+0x70); above 1 the planner
// 0x00650840 and the placer 0x00651560 (1). No pole records: the valid
// count 0. When placing, every step valid, two anchor points or more
// (unsigned count) and no failure reason: AddEdgeConnection (the word, the
// type); when it answers and +0x50 is set, ApplyNeighborConnections. Then,
// +0x50 clear (re-read), the tool's slot 82 (+0x148) with 0. The records
// cleared again. AL 1.
bool __fastcall DoNetworkSpecificPlacement(char* self, void*, uint32_t word, uint32_t place)
{
    static bool logged = false;
    FirstCall("PowerLineDoNetworkSpecificPlacement", logged);
    At<uint32_t>(self, power::kCount384) = 0;
    At<uint32_t>(self, power::kCount388) = 0;
    CallThis<void>(addr::cSC4PowerLineTool_ClearPoleRecords, self);
    CallThis<char*, char*, char*>(addr::SC4Vector16Vector_Erase, self + power::kRuns,
                                  VectorAt(self, power::kRuns).begin, VectorAt(self, power::kRuns).end);
    const int32_t steps = At<int32_t>(self, field::kStepCount);
    At<int32_t>(self, field::kValidSteps) = steps;
    if (steps > 1) {
        CallThis<void>(addr::cSC4PowerLineTool_PlanPoles, self);
        CallThis<void, uint32_t>(addr::cSC4PowerLineTool_PlacePoles, self, 1u);
    }
    const Vector& poles = VectorAt(self, power::kPoles);
    if (static_cast<int32_t>(poles.end - poles.begin) / static_cast<int32_t>(pole::kSize) == 0)
        At<int32_t>(self, field::kValidSteps) = 0;
    if (static_cast<uint8_t>(place) != 0 &&
        At<int32_t>(self, field::kValidSteps) >= At<int32_t>(self, field::kStepCount)) {
        const Vector& anchors = VectorAt(self, field::kAnchors);
        const uint32_t count = static_cast<uint32_t>(static_cast<int32_t>(anchors.end - anchors.begin) >> 3);
        if (count >= 2 && At<uint32_t>(self, field::kFailure) == 0 &&
            CallThis<uint8_t, uint32_t, uint32_t>(addr::cSC4NetworkTool_AddEdgeConnection, self, word,
                                                  At<uint32_t>(self, field::kType)) &&
            At<uint8_t>(self, field::kReplacing))
            CallThis<void>(addr::cSC4NetworkTool_ApplyNeighborConnections, self);
    }
    if (!At<uint8_t>(self, field::kReplacing))
        Virtual<void>(self, 0x148, 0u);
    CallThis<void>(addr::cSC4PowerLineTool_ClearPoleRecords, self);
    return true;
}

// The ten arguments of 0x006520C0 (ret 0x28): the rectangle, a start, an
// end and a step over a vector of points, the vector, two objects.
void WalkWire(char* self, const uint32_t* rect, int32_t start, int32_t end, int32_t stride, Vector* points, void* a,
              void* b)
{
    CallThis<void, uint32_t, uint32_t, uint32_t, uint32_t, int32_t, int32_t, int32_t, Vector*, void*, void*>(
        addr::cSC4PowerLineTool_ExtendAcrossZone, self, rect[0], rect[1], rect[2], rect[3], start, end, stride,
        points, a, b);
}

// 0x006522F0 slot 10 UpdateOnZoneChange (ret 0x14: x1, z1, x2, z2, a flag):
// nothing when the flag is 0. Else +0x50 set; two lists of the STL
// allocator (A for wires, B for poles) and a list of the node allocator
// (cleared) with the occupants from (x1, z1) to (x2, z2) (the occupant
// manager 0x00B43D0C; kept for the end). For each occupant:
//   - a power pole (0xA8C86142): its position's cell (slot 0x20, manager
//     slot 0x4C); outside the rectangle (signed) the pole released; inside,
//     for each of its wires k (its slot 0x10 the count): the points (slot
//     0x14: k, a vector), with more than one: from the pole's end (1 to n by
//     1 when the first point is the pole's cell, else n-1 to 0 by -1), the
//     walk 0x006520C0 (the rectangle, start, end, step, the points, the
//     pole, the wire's slot 0x1C (k)); the points freed; the pole appended
//     to B (kept referenced);
//   - else a wire (0xC98F3B24): its two poles (slot 0xC); compared with
//     those of each wire in A (each pair released at once): sharing one, the
//     wire released; else appended to A; its two poles released;
//   - the occupant released.
// Then for each wire in A: its two poles (slot 0xC); both set and neither
// in B: the points between them (the first pole's slot 0x40: the second, a
// vector); with more than one, the wire's cell (slot 0x14) looked up among
// them; found at i: SC4Network::ConnectEnds both ways (0) and the walk
// 0x006520C0 twice (i, n, 1, the points, the first pole, the second; i, n,
// -1, the second, the first); the points freed; the poles released; the
// wire released. Each pole in B: its slot 0x18 (1), the occupant manager's
// slot 0x54 (its +8 part, 1, 0), released. The node list cleared (Clear2)
// and its head given back; A's and B's nodes and heads freed.
void __fastcall UpdateOnZoneChange(char* self, void*, int32_t x1, int32_t z1, int32_t x2, int32_t z2, uint32_t flag)
{
    static bool logged = false;
    FirstCall("PowerLineUpdateOnZoneChange", logged);
    if (flag == 0)
        return;
    At<uint8_t>(self, field::kReplacing) = 1;
    const uint32_t rect[4] = {static_cast<uint32_t>(x1), static_cast<uint32_t>(z1), static_cast<uint32_t>(x2),
                              static_cast<uint32_t>(z2)};
    Node* const wires = reinterpret_cast<Node*>(Alloc(0xC));
    wires->next = wires;
    wires->previous = wires;
    Node* const poles = reinterpret_cast<Node*>(Alloc(0xC));
    poles->next = poles;
    poles->previous = poles;
    Node* list = NewList();
    CallThis<void>(addr::SC4List_Clear2, &list);
    void* const manager = Global<void*>(addr::SC4_NetworkService);
    {
        uint32_t low[2] = {rect[0], rect[1]};
        uint32_t high[2] = {rect[2], rect[3]};
        Virtual<void>(manager, kByCityCells, &list, low, high, 0u, 0xFFFFFFFFu);
    }
    Node* const head = list;
    for (Node* node = head->next; node != head; node = node->next) {
        void* const occupant = node->value;
        void* pole = nullptr;
        if (Virtual<uint8_t>(occupant, 0x0, kIidPowerPole, &pole)) {
            uint32_t position[3] = {0, 0, 0};
            Virtual<void>(occupant, 0x20, position);
            int32_t cx = 0, cz = 0;
            Virtual<void>(Global<void*>(addr::SC4_NetworkService), kCellOfPosition, position[0], position[2], &cx,
                          &cz);
            if (cx < x1 || cx > x2 || cz < z1 || cz > z2) {
                ReleaseOf(pole);
            } else {
                const int32_t count = Virtual<int32_t>(pole, 0x10);
                for (int32_t k = 0; k < count; ++k) {
                    Vector points = {nullptr, nullptr, nullptr};
                    Virtual<void>(pole, 0x14, k, &points);
                    const int32_t n = static_cast<int32_t>(points.end - points.begin) >> 3;
                    if (n > 1) {
                        int32_t start = 0, end = 0, stride = 0;
                        if (At<int32_t>(points.begin, 0) == cx && At<int32_t>(points.begin, 4) == cz) {
                            start = 1;
                            end = n;
                            stride = 1;
                        } else {
                            start = n - 1;
                            end = 0;
                            stride = -1;
                        }
                        void* const wire = Virtual<void*>(pole, 0x1C, k);
                        WalkWire(self, rect, start, end, stride, &points, pole, wire);
                    }
                    if (points.begin)
                        Free(points.begin);
                }
                PushBack(poles, pole);
            }
        } else {
            void* wire = nullptr;
            if (Virtual<uint8_t>(occupant, 0x0, kIidWire, &wire)) {
                void* a = nullptr;
                void* b = nullptr;
                uint32_t third = flag;
                Virtual<void>(wire, 0xC, &a, &b, &third);
                bool shared = false;
                for (Node* other = wires->next; other != wires; other = other->next) {
                    void* p = nullptr;
                    void* q = nullptr;
                    Virtual<void>(other->value, 0xC, &p, &q, &third);
                    if (p)
                        ReleaseOf(p);
                    if (q)
                        ReleaseOf(q);
                    if (a == p || b == p || a == q || b == q) {
                        shared = true;
                        break;
                    }
                }
                if (shared)
                    ReleaseOf(wire);
                else
                    PushBack(wires, wire);
                if (a)
                    ReleaseOf(a);
                if (b)
                    ReleaseOf(b);
            }
        }
        ReleaseOf(occupant);
    }
    for (Node* node = wires->next; node != wires; node = node->next) {
        void* const wire = node->value;
        void* p = nullptr;
        void* q = nullptr;
        uint32_t third = 0;
        Virtual<void>(wire, 0xC, &p, &q, &third);
        if (p && q) {
            bool inB = false;
            for (Node* b = poles->next; b != poles; b = b->next)
                if (p == b->value || q == b->value) {
                    inB = true;
                    break;
                }
            if (!inB) {
                Vector points = {nullptr, nullptr, nullptr};
                Virtual<void>(p, 0x40, q, &points);
                const int32_t n = static_cast<int32_t>(points.end - points.begin) >> 3;
                if (n > 1) {
                    int32_t wx = 0, wz = 0;
                    Virtual<void>(wire, 0x14, &wx, &wz);
                    for (int32_t i = 0; i < n; ++i) {
                        if (At<int32_t>(points.begin, i * 8) == wx && At<int32_t>(points.begin, i * 8 + 4) == wz) {
                            CallThis<void, void*, uint32_t>(addr::SC4Network_ConnectEnds, p, q, 0u);
                            CallThis<void, void*, uint32_t>(addr::SC4Network_ConnectEnds, q, p, 0u);
                            WalkWire(self, rect, i, n, 1, &points, p, q);
                            WalkWire(self, rect, i, n, -1, &points, q, p);
                            break;
                        }
                    }
                }
                if (points.begin)
                    Free(points.begin);
            }
        }
        if (p)
            ReleaseOf(p);
        if (q)
            ReleaseOf(q);
        ReleaseOf(wire);
    }
    for (Node* node = poles->next; node != poles; node = node->next) {
        char* const p = static_cast<char*>(node->value);
        Virtual<void>(p, 0x18, 1u);
        Virtual<void>(manager, kRemoveHighlight, p + 8, 1u, 0u);
        ReleaseOf(p);
    }
    CallThis<void>(addr::SC4List_Clear2, &list);
    Virtual<void>(ListNodeAllocator(), kDeallocate, static_cast<void*>(list));
    for (Node* n = poles->next; n != poles;) {
        Node* const freed = n;
        n = n->next;
        Free(freed);
    }
    poles->next = poles;
    poles->previous = poles;
    Free(poles);
    for (Node* n = wires->next; n != wires;) {
        Node* const freed = n;
        n = n->next;
        Free(freed);
    }
    wires->next = wires;
    wires->previous = wires;
    Free(wires);
}

#define OPENSC4_DETOUR(address, fn) OPENSC4_NETTOOL_DETOUR(address, fn)

const Detour kPower2[] = {
    OPENSC4_DETOUR(addr::cSC4PowerLineTool_PolePosition, PolePosition),
    OPENSC4_DETOUR(addr::cSC4PowerLineTool_HasPoleRecordWithKey, HasPoleRecordWithKey),
    OPENSC4_DETOUR(addr::cSC4PowerLineTool_HasPoleInLine, HasPoleInLine),
    OPENSC4_DETOUR(addr::cSC4PowerLineTool_FindDiagonalCell, FindDiagonalCell),
    OPENSC4_DETOUR(addr::cSC4PowerLineTool_LinkPoles, LinkPoles),
    OPENSC4_DETOUR(addr::cSC4PowerLineTool_ClearPoleRecords, ClearPoleRecords),
    OPENSC4_DETOUR(addr::cSC4PowerLineTool_FindWireAt, FindWireAt),
    OPENSC4_DETOUR(addr::cSC4PowerLineTool_ClassifyCell, ClassifyCell),
    OPENSC4_DETOUR(addr::cSC4PowerLineTool_FindPoleAt, FindPoleAt),
    OPENSC4_DETOUR(addr::cSC4PowerLineTool_MakePole, MakePole),
    OPENSC4_DETOUR(addr::cSC4PowerLineTool_DoNetworkSpecificPlacement, DoNetworkSpecificPlacement),
    OPENSC4_DETOUR(addr::cSC4PowerLineTool_UpdateOnZoneChange, UpdateOnZoneChange),
};

#undef OPENSC4_DETOUR

} // namespace

bool InstallPower2() { return InstallDetours(kPower2, "power2"); }

} // namespace opensc4::nettool_sim
