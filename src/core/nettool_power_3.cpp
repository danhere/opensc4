// Our cSC4NetworkTool family's power line tool, third part (build 641; part
// w2 of docs/nettool.md): the two pole layout passes slot 53 calls, the pole
// planner 0x00650840 and the pole placer 0x00651560. The same group as the
// first two parts, simulators.nettool.power (src/core/nettool_power.cpp
// registers it; its install calls InstallPower3 for this list). Calls to
// other functions, these two included (the placer calls both again), go to
// the game's addresses, so each detour stands alone and
// OPENSC4_NETTOOL_SKIP bisects them.
//
// The planner (no arguments, ret 0): +0x384 cleared, the pole records
// cleared (0x006505D0), the run vectors +0x378 erased (0x00650360). With
// the mark +0x74 inside the valid steps and above 0, the first cell of that
// step (to its last, unsigned) on the first anchor point's row or column
// (or any, without anchor points) is the cell that forces a pole. Then each
// valid step (+0x78, signed) and each of its cells (first to last,
// signed): the cell kind (0x0064FD00); a pole is forced at that cell, at the
// next anchor point (and the last cell of the last valid step) and at a
// diagonal cell (0x0064F230) a spacing (+0x3A4, unsigned) of steps after
// the last pole. Kind 0 (free): an existing pole at the cell (0x0064FFE0)
// takes a pole on a diagonal cell (released; off a diagonal cell the
// reference is kept: the game's leak); else a wire at the cell
// (0x0064FBF0): its two poles become the record's key when the drag is in
// the steps before +0x74 and they are on one row or column and either no
// record has that key and neither pole is in line with a record (0x0064F170,
// 0x0064F1D0) or the pole is forced; else they are released. A diagonal
// cell without a forced pole gets one when the line of sight from the last
// pole's top to the next cell's pole top (0x0064EFE0 plus +0x3A8) is clear
// (the renderer 0x00B43DD0 slot 0xF4: start, direction, hit out; the hit
// no nearer than the pole). A pole: the record (0x00650760: the cell, the
// "a pole before" byte, the run count, the key), the cells since the last
// pole as a new run vector (+0x378), the run emptied, the last pole's top
// moved here. A cell without a pole before the last valid step's last cell
// (unsigned) joins the run (x, z, 0, the diagonal byte) and +0x384 counts
// it. Kind 1 (a zone the line may not cross): a lone record without links
// is taken back (its objects not released: the game's), or the pending
// cell (above 0) gets a record when a pole was before. Kinds 2 and up
// (flora, a lot, another network): with a pole before or a forced pole, the
// failure reason (+0x20C 0xF0000012 for kind 2, 0xF0000013 for kind 3) and,
// for kind 2 or a forced pole, a lone record without links taken back
// (its objects released). Then a lone record without links is taken back,
// +0x78 is the last step with a free cell plus 1 and +0x388 the record
// count.
//
// The placer (ret 4: a byte, 1 from slot 53 and 0x006520C0, 0 when it
// calls itself): for each vector of +0x398 (32-bit dragged cell indices),
// the first record of each cell (only records without a pole unless +0x50)
// in a local vector, and each two of them whose cells are one apart (|dz| +
// |dx| == 1.0) linked (0x0064FB40, word -1); the vectors erased
// (0x00650250). Each record
// gets its pole (0x00650140). Then for each record: its two key poles'
// cells (slot 0x20), the two connected (SC4Network::ConnectEnds with +0x50,
// else cSC4PowerPoleOccupant::Unwire) both ways; each key pole whose byte
// (+0x2C, +0x2D) is set and the record's pole referenced into a local
// vector of pairs; without +0x50 the key moved into a node of the list
// +0x394, else released; the key cleared. For each link to a record whose
// pole is not wired to this one (slot 0x1C of each wire index below slot
// 0x10's count), with +0x50: the link's run (the word, when not negative)
// gets a new cSC4PowerLineOccupant (0x58 bytes) at each of its cells
// without one (preview byte "not +0x50", the two poles ordered by z then x
// (unsigned), its position the cell's pole position), and the two poles
// wired to each other (0x0064E3E0, the run on the side of the lower pole).
// With +0x50 the run cells of links with a word above 0 let their wires go.
// Each record's pole: new ones (+0x14) highlighted off by the occupant
// manager (+0x48 slot 0x50); with +0x50 its slot 0x24, else its +8 part's
// slot 0x44 (3, 0) and the pole referenced into the list +0x38C (new) or
// +0x390; the record lets it go. Each pair: both cells (slot 0x20); without
// +0x50 the two poles wired to each other with no run; with +0x50 and the
// argument set, the records cleared, the runs emptied and the drag redone
// between the two poles' cells (the tool's slot 0x48, DrawNetworkLine, the
// NAM-guarded 0x0063AF40, through the vtable as the game), +0x78 = +0x70,
// the planner and the placer (0) again at their addresses; both let go.
//
// What the game leaves on its stack, and why it does not matter here:
//   - The planner's record key (two words, two bytes, two padding bytes) is
//     handed whole to 0x00650760, which copies its three words into the
//     record (+0x24, +0x28, +0x2C). The padding (record +0x2E, +0x2F) is
//     never written by the planner; the bytes (+0x2C, +0x2D) are not
//     written before the first record when the first pole is placed
//     before any wire was looked at. Both are read nowhere for such a
//     record: the placer reads +0x2C/+0x2D only when +0x24/+0x28 is set
//     (and the planner sets the bytes with the words), 0x0064F170 compares
//     the words first against two set pointers, the copies copy the word,
//     and the node of the list +0x394 that gets +0x2C..+0x2F as its +0x10 is
//     never read there (slot 16 reads +0x8 and +0xC). Ours writes zeros.
//   - The "a pole before" byte goes to 0x00650760 as a whole word whose upper
//     bytes the game never writes; 0x00650760 reads only the byte. Ours
//     passes the byte.
//   - The placer's pairs get as their third word the local index vector's
//     capacity pointer of the last +0x398 vector, or a never-written stack
//     word when +0x398 is empty; the word is never read (the pairs are read
//     at +0 and +4 only, copied whole). Ours keeps the same pointer and 0
//     where the game has the leftover.
//   - SetPreview (the new wire's slot 0x1C) is pushed the whole ECX with the
//     flag in its low byte and the upper bytes left by the call before;
//     cSC4PowerLineOccupant::SetPreview 0x006493A0 reads the byte. Ours
//     passes the flag.
//   - 0x00650760 (the game's) leaves its record's byte +0x14 as its own
//     stack had it, and the placer tests +0x14 where MakePole did not write
//     it (a pole found at the cell and no exemplar for its sides): what
//     that byte is depends on what ran below the caller's frame. That was
//     already so with part w1's helpers (ours, other frames) under the
//     game's planner; it is the game's 0x00650760 that reads its stack.
//
// NAM. Neither function is guarded, patched or called by NAM's DLL (its
// patch sites are all outside 0x0064EFE0-0x00652A6B; none of its patches
// calls into them); the records, runs, lists and occupants stay in the
// game's memory in the game's layout. The placer's slot 0x48 call reaches
// DrawNetworkLine 0x0063AF40 (NAM-guarded, the game's) through the tool's
// vtable, as the game calls it.
//
// Floats. Planner, the line of sight (nothing on the x87 stack across a
// call): the next cell's pole position (0x0064EFE0, three floats); dy =
// (y + the float +0x3A8, a double) - the last top's y, stored as a float;
// dx = x - the last top's x as a double (never stored); dz likewise, stored
// as a float; the squared length (dz * float(dz) + float(dy)^2) + dx^2 as a
// double, stored as a float; the direction's scale the float 1.0 at
// 0x00B0C264 over the double's square root, stored as a float; the
// direction (dx, float(dy), float(dz)) times it, each stored as a float. The
// hit test: the hit's three differences from the last top as doubles, s =
// sqrt((dz^2 + dy^2) + dx^2) as a double; the pole goes there when
// sqrt(float squared length) >= s (`fcompp`, `test ah,1; jne`: below and
// unordered skip). The new top: the pole position's y plus +0x3A8 (two
// floats summed as a double), stored as a float. The three vectors start as
// the float at 0x00B467F0 (zero: nothing in the exe writes it), read where
// the game reads it. All at the x87's 53-bit precision, which double
// arithmetic repeats (sqrt is correctly rounded on both). Placer: |z1 - z2|
// + |x1 - x2| of the cells as unsigned 32-bit values (FILD plus 2^32 when
// negative: exact), compared with the float 1.0 at 0x00A81228 (`fucompp`,
// `test ah,0x44; jp`: equal only); exact in a double.
//
// Registers: the direct callers (the placer itself for both, the game's
// 0x006520C0, slot 10's walk, at 0x006522BF / 0x006522C8, and slot 53
// ours) were scanned (scratch regscan.py / eaxscan.py): none reads ECX, EDX
// or EAX after either call. Neither function reads a register beyond ECX at
// entry. No naked thunks.
//
// Calls, each count from the asm's pushes and the callee's own `ret N`:
// planner: 0x006505D0 none, SC4Vector16Vector::Erase 0x00650360 two (ret 8),
// FindDiagonalCell 0x0064F230 two (ret 8), ClassifyCell 0x0064FD00 two (ret
// 8), FindPoleAt 0x0064FFE0 cdecl three, FindWireAt 0x0064FBF0 three (ret
// 0xC), HasPoleRecordWithKey 0x0064F170 one (ret 4), HasPoleInLine 0x0064F1D0
// one (ret 4), PolePosition 0x0064EFE0 three (ret 0xC), AddPoleRecord
// 0x00650760 four (ret 0x10), SC4Vector16Vector::UninitializedCopy
// 0x0064F670 cdecl four (the fourth unread), SC4Vector16::CopyConstruct
// 0x0064F610 one (ret 4), the STL allocator 0x0090CF54 and the free
// 0x0090CF63 cdecl one; a wire's slot 0xC three (two poles, a byte out); a
// pole's slot 0x20 two (x out, z out: 0x00649210) and slot 8 none; the
// renderer 0x00B43DD0 slot 0xF4 five (start, direction, hit, 0, 0; AL).
// Placer: SC4Vector12::Copy 0x006143E0 cdecl five (three zeros, the
// argument's address, 0: the two last unread; EAX the end), SC4::memmove
// 0x009EEB70 cdecl three, LinkPoles 0x0064FB40 three (ret 0xC),
// SC4IntVectorVector::EraseRange 0x00650250 two (ret 8), MakePole 0x00650140
// one (ret 4), SC4Network::ConnectEnds 0x0064DA00 two (ret 8),
// cSC4PowerPoleOccupant::Unwire 0x00649910 two (ret 8), WireTo 0x0064E3E0
// two (ret 8), cSC4PowerLineOccupant::Allocate 0x0064A2E0 cdecl one (0x58)
// and its constructor 0x0064A270 none, PolePosition 0x0064EFE0 three (ret
// 0xC), ClearPoleRecords 0x006505D0 none, the planner 0x00650840 none and
// the placer 0x00651560 one (ret 4); the STL allocator and free; a pole's
// slots 4 and 8 none, 0x10 none (the wire count), 0x1C one, 0x20 two, 0x24
// none, its +8 part's slot 0x44 two; the new wire's slots 4 none, 0x1C one
// (SetPreview), 0x10 three (SetConnectedPoles), its +4 part's slot 0xC none
// and 0x24 one; the occupant manager (+0x48) slot 0x50 two; the tool's slot
// 0x48 four.
#include "core/nettool_power.h"
#include "core/nettool_sim.h"

#include "core/game.h"
#include "core/log.h"

#include <opensc4/addresses.h>

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace opensc4::nettool_sim {
namespace {

// The record key the planner keeps on its stack (12 bytes, copied whole by
// 0x00650760 into the record's +0x24).
struct PoleKey {
    void* first;
    void* second;
    uint8_t firstWired;
    uint8_t secondWired;
    uint8_t padding[2];
};
static_assert(sizeof(PoleKey) == 12);

// A run element (16 bytes): x, z, a referenced wire, the diagonal byte.
inline constexpr std::size_t kRunSize = 0x10, kRunWire = 0x8, kRunDiagonal = 0xC;
inline constexpr std::size_t kVectorSize = 0xC; // a vector object
inline constexpr std::size_t kMark = 0x74;      // the step the start test looks at
// The renderer's line test against the city (start, direction, hit out, 0, 0).
inline constexpr std::size_t kLineHit = 0xF4;
inline constexpr uint32_t kFlora = 0xF0000012, kOtherNetwork = 0xF0000013;
// The occupant manager's highlight slot, a pole's interface slots.
inline constexpr std::size_t kSetHighlight = 0x50, kWireCount = 0x10, kWireAt = 0x1C, kCellOf = 0x20;
inline constexpr std::size_t kPoleShow = 0x24, kPartHighlight = 0x44;
// The power line occupant (0x58 bytes) and its slots.
inline constexpr uint32_t kLineSize = 0x58;
inline constexpr std::size_t kSetConnectedPoles = 0x10, kSetPreview = 0x1C, kPartSetup = 0xC, kPartPosition = 0x24;
// The tool's DrawNetworkLine (slot 18).
inline constexpr std::size_t kDrawNetworkLine = 0x48;

char* Alloc(uint32_t bytes) { return CallCdecl<char*, uint32_t>(addr::SC4_stl_allocate, bytes); }
void Free(void* p) { CallCdecl<void, void*>(addr::SC4_Free, p); }
Vector& VectorAt(char* object, std::size_t offset) { return At<Vector>(object, offset); }
float AsFloat(uint32_t bits) { return std::bit_cast<float>(bits); }
uint32_t Bits(float value) { return std::bit_cast<uint32_t>(value); }
bool NotNull(char* p, std::size_t offset) { return reinterpret_cast<std::uintptr_t>(p) + offset != 0; }

// The growth the game's inline vector code uses: the size plus the size or
// 1 (unsigned compare).
uint32_t Grown(uint32_t size) { return size + (size < 1u ? 1u : size); }

int32_t Elements(const Vector& v, int32_t size)
{
    return static_cast<int32_t>(v.end - v.begin) / size;
}

// The planner's inline copy of a run vector into `dest` (as
// SC4Vector16::CopyConstruct, with the game's writes): the three words
// cleared, the elements' buffer (none for an empty run), each element's
// three words and its byte copied where the destination is set.
void CopyRunInline(char* dest, const Vector& run)
{
    const int32_t n = static_cast<int32_t>(run.end - run.begin) >> 4;
    At<char*>(dest, 0) = nullptr;
    At<char*>(dest, 4) = nullptr;
    At<char*>(dest, 8) = nullptr;
    char* p = nullptr;
    if (n != 0)
        p = Alloc(static_cast<uint32_t>(n) << 4);
    At<char*>(dest, 0) = p;
    At<char*>(dest, 4) = p;
    At<char*>(dest, 8) = p + (static_cast<uint32_t>(n) << 4);
    for (char* s = run.begin; s != run.end; s += kRunSize, p += kRunSize)
        if (p) {
            At<uint32_t>(p, 0) = At<uint32_t>(s, 0);
            At<uint32_t>(p, 4) = At<uint32_t>(s, 4);
            At<uint32_t>(p, 8) = At<uint32_t>(s, 8);
            At<uint8_t>(p, kRunDiagonal) = At<uint8_t>(s, kRunDiagonal);
        }
    At<char*>(dest, 4) = p;
}

// The run appended to the tool's +0x378 as the planner's pole branch does
// (inline copy; the growing path through the game's uninitialized copy).
void AppendRunInline(char* self, const Vector& run)
{
    Vector& runs = VectorAt(self, power::kRuns);
    char* const end = runs.end;
    if (end != runs.capacity) {
        if (end)
            CopyRunInline(end, run);
        runs.end += kVectorSize;
        return;
    }
    const uint32_t cap = Grown(static_cast<uint32_t>(Elements(runs, kVectorSize)));
    char* const buffer = cap ? Alloc(cap * kVectorSize) : nullptr;
    uint8_t unread = 0;
    char* last = CallCdecl<char*, char*, char*, char*, uint8_t*>(addr::SC4Vector16Vector_UninitializedCopy,
                                                                 runs.begin, end, buffer, &unread);
    if (last)
        CopyRunInline(last, run);
    last += kVectorSize;
    for (char* p = runs.begin; p != runs.end; p += kVectorSize)
        if (void* const elements = At<void*>(p, 0))
            Free(elements);
    if (runs.begin)
        Free(runs.begin);
    runs.begin = buffer;
    runs.end = last;
    runs.capacity = buffer + cap * kVectorSize;
}

// The same through SC4Vector16::CopyConstruct (the zone branch's code).
void AppendRunCopy(char* self, Vector* run)
{
    Vector& runs = VectorAt(self, power::kRuns);
    char* const end = runs.end;
    if (end != runs.capacity) {
        if (end)
            CallThis<void, Vector*>(addr::SC4Vector16_CopyConstruct, end, run);
        runs.end += kVectorSize;
        return;
    }
    const uint32_t cap = Grown(static_cast<uint32_t>(Elements(runs, kVectorSize)));
    char* const buffer = cap ? Alloc(cap * kVectorSize) : nullptr;
    uint8_t unread = 0;
    char* last = CallCdecl<char*, char*, char*, char*, uint8_t*>(addr::SC4Vector16Vector_UninitializedCopy,
                                                                 runs.begin, end, buffer, &unread);
    if (last)
        CallThis<void, Vector*>(addr::SC4Vector16_CopyConstruct, last, run);
    last += kVectorSize;
    char* const oldEnd = runs.end;
    for (char* p = runs.begin; p != oldEnd; p += kVectorSize)
        if (void* const elements = At<void*>(p, 0))
            Free(elements);
    if (runs.begin)
        Free(runs.begin);
    runs.end = last;
    runs.begin = buffer;
    runs.capacity = buffer + cap * kVectorSize;
}

// A cell appended to the planner's run: (x, z, 0, the diagonal byte).
void AppendCell(Vector& run, uint32_t x, uint32_t z, uint8_t diagonal)
{
    if (run.end != run.capacity) {
        if (char* const e = run.end) {
            At<uint32_t>(e, 0) = x;
            At<uint32_t>(e, 4) = z;
            At<uint32_t>(e, kRunWire) = 0;
            At<uint8_t>(e, kRunDiagonal) = diagonal;
        }
        run.end += kRunSize;
        return;
    }
    const uint32_t size = static_cast<uint32_t>(static_cast<int32_t>(run.end - run.begin) >> 4);
    const uint32_t cap = Grown(size);
    char* const buffer = cap ? Alloc(cap << 4) : nullptr;
    char* p = buffer;
    for (char* s = run.begin; s != run.end; s += kRunSize, p += kRunSize)
        if (p) {
            At<uint32_t>(p, 0) = At<uint32_t>(s, 0);
            At<uint32_t>(p, 4) = At<uint32_t>(s, 4);
            At<uint32_t>(p, 8) = At<uint32_t>(s, 8);
            At<uint8_t>(p, kRunDiagonal) = At<uint8_t>(s, kRunDiagonal);
        }
    if (p) {
        At<uint32_t>(p, 0) = x;
        At<uint32_t>(p, 4) = z;
        At<uint32_t>(p, kRunWire) = 0;
        At<uint8_t>(p, kRunDiagonal) = diagonal;
    }
    if (run.begin)
        Free(run.begin);
    run.begin = buffer;
    run.end = p + kRunSize;
    run.capacity = buffer + (cap << 4);
}

// The last pole record taken back when it has no links (its links' buffer
// freed; its objects released only where the game does).
void DropLoneRecord(char* self, bool release)
{
    Vector& poles = VectorAt(self, power::kPoles);
    if (poles.begin == poles.end)
        return;
    char* const record = poles.end - pole::kSize;
    if (At<char*>(record, pole::kLinks) != At<char*>(record, pole::kLinksEnd))
        return;
    if (release) {
        if (void* const first = At<void*>(record, pole::kObject24))
            ReleaseOf(first);
        if (void* const second = At<void*>(record, pole::kObject28))
            ReleaseOf(second);
    }
    poles.end -= pole::kSize;
    if (void* const links = At<void*>(poles.end, pole::kLinks))
        Free(links);
}

// The last valid step's last dragged cell (the steps' vector re-read).
uint32_t LastValidCell(char* self)
{
    const int32_t valid = At<int32_t>(self, field::kValidSteps);
    return At<uint32_t>(VectorAt(self, field::kSteps).begin + valid * static_cast<int32_t>(step::kSize) - 8, 0);
}

// The cell's pole position (0x0064EFE0, three floats out).
void PolePosition(char* self, uint32_t x, uint32_t z, uint32_t* out)
{
    CallThis<void, uint32_t, uint32_t, uint32_t*>(addr::cSC4PowerLineTool_PolePosition, self, x, z, out);
}

// A pole record appended (0x00650760): the dragged cell, the "a pole
// before" byte (the game pushes it as a whole word whose upper bytes it
// never wrote; the callee reads the byte), the run count, the key.
void AddRecord(char* self, int32_t cell, uint8_t poleBefore, PoleKey* key)
{
    const uint32_t count = static_cast<uint32_t>(Elements(VectorAt(self, power::kRuns), kVectorSize));
    CallThis<void, int32_t, uint32_t, uint32_t, PoleKey*>(addr::cSC4PowerLineTool_AddPoleRecord, self, cell,
                                                          poleBefore, count, key);
}

// ====================================================================================
// The pole planner
// ====================================================================================

// 0x00650840 (no arguments): see the file comment.
void __fastcall PlanPoles(char* self, void*)
{
    static bool logged = false;
    FirstCall("PowerLinePlanPoles", logged);
    uint32_t top[3]; // the last pole's top (+0x60 of the game's frame)
    top[0] = top[1] = top[2] = Global<uint32_t>(addr::SC4_FloatB467F0);
    int32_t lastPoleStep = -1, pendingCell = -1, startCell = -1;
    int32_t lastFreeStep = 0;
    uint8_t place = 0, poleBefore = 0;
    int32_t records = 0;
    Vector run = {nullptr, nullptr, nullptr};
    const int32_t anchorCount =
        static_cast<int32_t>(VectorAt(self, field::kAnchors).end - VectorAt(self, field::kAnchors).begin) >> 3;
    int32_t anchor = 0;
    At<uint32_t>(self, power::kCount384) = 0;
    PoleKey key = {nullptr, nullptr, 0, 0, {0, 0}};
    CallThis<void>(addr::cSC4PowerLineTool_ClearPoleRecords, self);
    CallThis<char*, char*, char*>(addr::SC4Vector16Vector_Erase, self + power::kRuns,
                                  VectorAt(self, power::kRuns).begin, VectorAt(self, power::kRuns).end);

    // The cell that forces the first pole of the marked step.
    const int32_t mark = At<int32_t>(self, kMark);
    if (mark < At<int32_t>(self, field::kValidSteps) && mark > 0) {
        const std::size_t at = static_cast<std::size_t>(mark) * step::kSize;
        uint32_t c = At<uint32_t>(VectorAt(self, field::kSteps).begin + at, 0);
        if (c <= At<uint32_t>(VectorAt(self, field::kSteps).begin + at, step::kLastCell)) {
            for (;;) {
                const Vector& anchors = VectorAt(self, field::kAnchors);
                if (anchors.begin == anchors.end) {
                    startCell = static_cast<int32_t>(c);
                    break;
                }
                char* const cell = VectorAt(self, field::kCells).begin + c * kPointSize;
                if (At<uint32_t>(cell, 0) == At<uint32_t>(anchors.begin, 0) ||
                    At<uint32_t>(cell, 4) == At<uint32_t>(anchors.begin, 4)) {
                    startCell = static_cast<int32_t>(c);
                    break;
                }
                ++c;
                if (c > At<uint32_t>(VectorAt(self, field::kSteps).begin + at, step::kLastCell))
                    break;
            }
        }
    }

    for (int32_t s = 0; s < At<int32_t>(self, field::kValidSteps); ++s) {
        const int32_t last =
            At<int32_t>(VectorAt(self, field::kSteps).begin + s * static_cast<int32_t>(step::kSize), step::kLastCell);
        const int32_t diagonal =
            CallThis<int32_t, int32_t, int32_t>(addr::cSC4PowerLineTool_FindDiagonalCell, self, s, anchor);
        for (int32_t i = At<int32_t>(VectorAt(self, field::kSteps).begin + s * static_cast<int32_t>(step::kSize), 0);
             i <= last; ++i) {
            char* const cell = VectorAt(self, field::kCells).begin + i * static_cast<int32_t>(kPointSize);
            const uint32_t x = At<uint32_t>(cell, 0);
            const uint32_t z = At<uint32_t>(cell, 4);
            const uint32_t kind =
                CallThis<uint32_t, uint32_t, uint32_t>(addr::cSC4PowerLineTool_ClassifyCell, self, x, z);
            const uint8_t isDiagonal = i == diagonal ? 1 : 0;
            uint8_t forced = 0;
            bool placed = false;
            if (i == startCell) {
                forced = 1;
                place = 1;
                startCell = -1;
            }
            if (anchor < anchorCount) {
                char* const point = VectorAt(self, field::kAnchors).begin + anchor * static_cast<int32_t>(kPointSize);
                if ((At<uint32_t>(point, 0) == x && At<uint32_t>(point, 4) == z) ||
                    (s == At<int32_t>(self, field::kValidSteps) - 1 && i == last)) {
                    ++anchor;
                    forced = 1;
                    place = 1;
                }
            }
            const uint8_t hadPole = poleBefore; // the byte the later tests read
            if (isDiagonal && hadPole && lastPoleStep >= 0 &&
                static_cast<uint32_t>(s - lastPoleStep) >= At<uint32_t>(self, power::kTuning3A4)) {
                forced = 1;
                place = 1;
            }

            if (kind == 0) {
                At<uint32_t>(self, field::kFailure) = 0;
                lastFreeStep = s;
                pendingCell = i;
                void* existing = nullptr;
                bool toPole = false;
                if (CallCdecl<uint8_t, uint32_t, uint32_t, void**>(addr::cSC4PowerLineTool_FindPoleAt, x, z,
                                                                   &existing)) {
                    if (isDiagonal) {
                        place = 1;
                        ReleaseOf(existing);
                    }
                    // Off a diagonal cell the reference is kept (the game's).
                } else {
                    void* wire = nullptr;
                    if (CallThis<uint8_t, uint32_t, uint32_t, void**>(addr::cSC4PowerLineTool_FindWireAt, self, x, z,
                                                                      &wire)) {
                        void* first = nullptr;
                        void* second = nullptr;
                        uint8_t unread = 0;
                        Virtual<void>(wire, 0xC, &first, &second, &unread);
                        int32_t fx = 0, fz = 0, sx = 0, sz = 0;
                        Virtual<void>(first, kCellOf, &fx, &fz);
                        key.first = first;
                        key.second = second;
                        key.firstWired = 1;
                        key.secondWired = 1;
                        Virtual<void>(second, kCellOf, &sx, &sz);
                        bool keep = false;
                        if (s < At<int32_t>(self, kMark) && (fx == sx || fz == sz)) {
                            place = 1;
                            if (CallThis<uint8_t, PoleKey*>(addr::cSC4PowerLineTool_HasPoleRecordWithKey, self, &key) ||
                                CallThis<uint8_t, void*>(addr::cSC4PowerLineTool_HasPoleInLine, self, first) ||
                                CallThis<uint8_t, void*>(addr::cSC4PowerLineTool_HasPoleInLine, self, second)) {
                                if (forced)
                                    keep = true;
                                else
                                    place = 0;
                            } else {
                                keep = true;
                            }
                        }
                        if (!keep) {
                            ReleaseOf(first);
                            ReleaseOf(second);
                            key.first = nullptr;
                            key.second = nullptr;
                            key.firstWired = 1;
                            key.secondWired = 1;
                        }
                        ReleaseOf(wire);
                    }
                }
                if (!isDiagonal) {
                    toPole = place != 0;
                } else if (place) {
                    toPole = true;
                } else if (static_cast<uint32_t>(i) < LastValidCell(self) &&
                           VectorAt(self, power::kPoles).begin != VectorAt(self, power::kPoles).end) {
                    // The line of sight from the last top to the next cell's
                    // pole top (see Floats in the file comment).
                    const uint32_t zero = Global<uint32_t>(addr::SC4_FloatB467F0);
                    uint32_t at[3] = {zero, zero, zero};
                    uint32_t hit[3] = {zero, zero, zero};
                    uint32_t direction[3] = {zero, zero, zero};
                    char* const next = VectorAt(self, field::kCells).begin + (i + 1) * static_cast<int32_t>(kPointSize);
                    PolePosition(self, At<uint32_t>(next, 0), At<uint32_t>(next, 4), at);
                    const double height = static_cast<double>(At<float>(self, power::kTuning3A8));
                    const double y = static_cast<double>(AsFloat(at[1])) + height;
                    const double dx = static_cast<double>(AsFloat(at[0])) - static_cast<double>(AsFloat(top[0]));
                    const float dy = static_cast<float>(y - static_cast<double>(AsFloat(top[1])));
                    const double dz = static_cast<double>(AsFloat(at[2])) - static_cast<double>(AsFloat(top[2]));
                    const float dzf = static_cast<float>(dz);
                    const double squared = (dz * static_cast<double>(dzf) +
                                            static_cast<double>(dy) * static_cast<double>(dy)) +
                                           dx * dx;
                    const float squaredf = static_cast<float>(squared);
                    const double one = static_cast<double>(Global<float>(addr::SC4_FloatOneB0C264));
                    const float scale = static_cast<float>(one / std::sqrt(squared));
                    direction[0] = Bits(static_cast<float>(dx * static_cast<double>(scale)));
                    direction[1] = Bits(static_cast<float>(static_cast<double>(dy) * static_cast<double>(scale)));
                    direction[2] = Bits(static_cast<float>(static_cast<double>(dzf) * static_cast<double>(scale)));
                    if (Virtual<uint8_t>(Global<void*>(addr::SC4_View43DD0), kLineHit, top, direction, hit, 0u, 0u)) {
                        const double hx = static_cast<double>(AsFloat(hit[0])) - static_cast<double>(AsFloat(top[0]));
                        const double hy = static_cast<double>(AsFloat(hit[1])) - static_cast<double>(AsFloat(top[1]));
                        const double hz = static_cast<double>(AsFloat(hit[2])) - static_cast<double>(AsFloat(top[2]));
                        const double reach = std::sqrt((hz * hz + hy * hy) + hx * hx);
                        const double length = std::sqrt(static_cast<double>(squaredf));
                        toPole = length >= reach; // below or unordered: no pole
                    }
                }
                if (toPole) {
                    AddRecord(self, i, hadPole, &key);
                    AppendRunInline(self, run);
                    key.first = nullptr;
                    key.second = nullptr;
                    run.end = run.begin;
                    ++records;
                    key.firstWired = 1;
                    key.secondWired = 1;
                    poleBefore = 1;
                    PolePosition(self, x, z, top);
                    top[1] = Bits(static_cast<float>(static_cast<double>(AsFloat(top[1])) +
                                                     static_cast<double>(At<float>(self, power::kTuning3A8))));
                    lastPoleStep = s;
                    pendingCell = -1;
                    placed = true;
                }
                place = 0;
                if (placed)
                    continue;
            } else if (kind == 1) {
                if (records == 1 && pendingCell == -1) {
                    pendingCell = -1;
                    records = 0;
                    DropLoneRecord(self, false);
                } else if (pendingCell > 0 && hadPole) {
                    AddRecord(self, pendingCell, hadPole, &key);
                    AppendRunCopy(self, &run);
                    run.end = run.begin;
                    key.first = nullptr;
                    key.second = nullptr;
                    key.firstWired = 1;
                    key.secondWired = 1;
                    records = 0;
                    pendingCell = -1;
                    lastPoleStep = s;
                }
                poleBefore = 0;
                place = 1;
                continue;
            } else if ((hadPole && lastPoleStep >= 0) || forced) {
                bool refused = true;
                if (kind == 2) {
                    At<uint32_t>(self, field::kFailure) = kFlora;
                } else {
                    if (kind == 3)
                        At<uint32_t>(self, field::kFailure) = kOtherNetwork;
                    refused = forced != 0;
                }
                if (refused) {
                    if (records == 1 && pendingCell == -1) {
                        records = 0;
                        DropLoneRecord(self, true);
                    }
                    pendingCell = -1;
                    continue;
                }
            }
            // A cell without a pole joins the run before the last valid
            // step's last cell (unsigned).
            if (static_cast<uint32_t>(i) < LastValidCell(self)) {
                AppendCell(run, x, z, isDiagonal);
                ++At<uint32_t>(self, power::kCount384);
            }
        }
    }
    if (records == 1)
        DropLoneRecord(self, true);
    At<int32_t>(self, field::kValidSteps) = lastFreeStep + 1;
    At<uint32_t>(self, power::kCount388) =
        static_cast<uint32_t>(Elements(VectorAt(self, power::kPoles), static_cast<int32_t>(pole::kSize)));
    if (run.begin)
        Free(run.begin);
}

// ====================================================================================
// The pole placer
// ====================================================================================

// A pair (a pole, the record's pole, a word nobody reads) appended to the
// placer's local vector of 12-byte elements.
void AppendPair(Vector& pairs, void* first, void* second, uint32_t word)
{
    if (pairs.end != pairs.capacity) {
        if (char* const e = pairs.end) {
            At<void*>(e, 0) = first;
            At<void*>(e, 4) = second;
            At<uint32_t>(e, 8) = word;
        }
        pairs.end += 12;
        return;
    }
    const uint32_t cap = Grown(static_cast<uint32_t>(Elements(pairs, 12)));
    char* const buffer = cap ? Alloc(cap * 12) : nullptr;
    char* p = buffer;
    for (char* s = pairs.begin; s != pairs.end; s += 12, p += 12)
        if (p) {
            At<uint32_t>(p, 0) = At<uint32_t>(s, 0);
            At<uint32_t>(p, 4) = At<uint32_t>(s, 4);
            At<uint32_t>(p, 8) = At<uint32_t>(s, 8);
        }
    if (p) {
        At<void*>(p, 0) = first;
        At<void*>(p, 4) = second;
        At<uint32_t>(p, 8) = word;
    }
    if (pairs.begin)
        Free(pairs.begin);
    pairs.begin = buffer;
    pairs.end = p + 12;
    pairs.capacity = buffer + cap * 12;
}

// A node with `value` linked last into a list of the STL allocator (the
// game tests the node's +8 for null before the store).
void LinkLast(char* head, uint32_t bytes, const void* value, std::size_t valueBytes)
{
    char* const node = Alloc(bytes);
    if (NotNull(node, 8))
        for (std::size_t k = 0; k < valueBytes; k += 4)
            At<uint32_t>(node, 8 + k) = *reinterpret_cast<const uint32_t*>(static_cast<const char*>(value) + k);
    char* const last = At<char*>(head, 4);
    At<char*>(node, 0) = head;
    At<char*>(node, 4) = last;
    At<char*>(last, 0) = node;
    At<char*>(head, 4) = node;
}

// The two key poles connected both ways: SC4Network::ConnectEnds with
// +0x50, else Unwire.
void Connect(char* self, void* a, void* b)
{
    if (At<uint8_t>(self, field::kReplacing))
        CallThis<void, void*, uint32_t>(addr::SC4Network_ConnectEnds, a, b, 0u);
    else
        CallThis<void, void*, uint32_t>(addr::cSC4PowerPoleOccupant_Unwire, a, b, 0u);
}

// 0x00651560 (ret 4: a byte): see the file comment.
void __fastcall PlacePoles(char* self, void*, uint32_t again)
{
    static bool logged = false;
    FirstCall("PowerLinePlacePoles", logged);
    Vector pairs = {nullptr, nullptr, nullptr};
    pairs.end = CallCdecl<char*, char*, char*, char*, uint32_t*, uint32_t>(addr::SC4Vector12_Copy, nullptr, nullptr,
                                                                          nullptr, &again, 0u);
    // The cells' first records linked to their neighbours, per vector of
    // dragged cell indices (+0x398).
    uint32_t capacityWord = 0; // what the game keeps of the local vector
    const int32_t lists = Elements(VectorAt(self, power::kIntVectors), kVectorSize);
    if (lists > 0) {
        std::size_t offset = 0;
        int32_t left = lists;
        do {
            char* const cells = VectorAt(self, power::kIntVectors).begin + offset;
            const int32_t n = static_cast<int32_t>(At<char*>(cells, 4) - At<char*>(cells, 0)) >> 2;
            uint32_t* begin = nullptr;
            uint32_t* end = nullptr;
            uint32_t* capacity = nullptr;
            capacityWord = 0;
            for (int32_t k = 0; k < n; ++k) {
                char* r = VectorAt(self, power::kPoles).begin;
                char* const recordsEnd = VectorAt(self, power::kPoles).end;
                const uint32_t value = At<uint32_t*>(cells, 0)[k];
                if (r == recordsEnd)
                    continue;
                for (uint32_t j = 0; r != recordsEnd; r += pole::kSize, ++j) {
                    if ((At<uint8_t>(self, field::kReplacing) == 0 && At<void*>(r, pole::kOccupant) != nullptr) ||
                        At<uint32_t>(r, pole::kCell) != value)
                        continue;
                    if (end != capacity) {
                        if (end)
                            *end = j;
                        ++end;
                    } else {
                        const int32_t bytes = static_cast<int32_t>(reinterpret_cast<char*>(end) -
                                                                   reinterpret_cast<char*>(begin));
                        const uint32_t cap = Grown(static_cast<uint32_t>(bytes >> 2));
                        uint32_t* const buffer = cap ? reinterpret_cast<uint32_t*>(Alloc(cap * 4)) : nullptr;
                        uint32_t* p = buffer;
                        if (end != begin)
                            p = reinterpret_cast<uint32_t*>(
                                static_cast<char*>(CallCdecl<void*, void*, const void*, uint32_t>(
                                    addr::SC4_memmove, buffer, begin, static_cast<uint32_t>(bytes))) +
                                bytes);
                        *p = j;
                        end = p + 1;
                        if (begin)
                            Free(begin);
                        begin = buffer;
                        capacity = buffer + cap;
                    }
                    break;
                }
                capacityWord = static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(capacity));
            }
            const int32_t m = static_cast<int32_t>(end - begin);
            for (int32_t a = 0; a < m; ++a) {
                const uint32_t at = begin[a] * static_cast<uint32_t>(pole::kSize);
                for (int32_t b = 0; b < m; ++b) {
                    char* const records = VectorAt(self, power::kPoles).begin;
                    char* const ra = records + at;
                    char* const rb = records + begin[b] * static_cast<uint32_t>(pole::kSize);
                    // |z1 - z2| + |x1 - x2| as unsigned values (see Floats).
                    const double za = static_cast<double>(At<uint32_t>(ra, pole::kZ));
                    const double zb = static_cast<double>(At<uint32_t>(rb, pole::kZ));
                    const double xa = static_cast<double>(At<uint32_t>(ra, pole::kX));
                    const double xb = static_cast<double>(At<uint32_t>(rb, pole::kX));
                    const double sum = std::fabs(za - zb) + std::fabs(xa - xb);
                    if (sum == static_cast<double>(Global<float>(addr::SC4_FloatOneA81228)))
                        CallThis<void, char*, char*, uint32_t>(addr::cSC4PowerLineTool_LinkPoles, self, ra, rb,
                                                               0xFFFFFFFFu);
                }
            }
            if (begin)
                Free(begin);
            offset += kVectorSize;
        } while (--left != 0);
    }
    CallThis<char*, char*, char*>(addr::SC4IntVectorVector_EraseRange, self + power::kIntVectors,
                                  VectorAt(self, power::kIntVectors).begin, VectorAt(self, power::kIntVectors).end);
    {
        char* const first = VectorAt(self, power::kPoles).begin;
        char* const last = VectorAt(self, power::kPoles).end;
        for (char* r = first; r != last; r += pole::kSize)
            CallThis<void, char*>(addr::cSC4PowerLineTool_MakePole, self, r);
    }

    // Each record's key poles, links and wires.
    char* const recordsEnd = VectorAt(self, power::kPoles).end;
    for (char* r = VectorAt(self, power::kPoles).begin; r != recordsEnd; r += pole::kSize) {
        int32_t cells[4] = {-1, -1, -1, -1};
        if (void* const a = At<void*>(r, pole::kObject24))
            Virtual<void>(a, kCellOf, &cells[0], &cells[1]);
        if (void* const b = At<void*>(r, pole::kObject28))
            Virtual<void>(b, kCellOf, &cells[2], &cells[3]);
        if (void* const a = At<void*>(r, pole::kObject24)) {
            if (void* const b = At<void*>(r, pole::kObject28))
                Connect(self, a, b);
            if (At<uint8_t>(r, pole::kWord2C)) {
                void* const key = At<void*>(r, pole::kObject24);
                void* const occupant = At<void*>(r, pole::kOccupant);
                AddRefOf(key);
                AddRefOf(At<void*>(r, pole::kOccupant));
                AppendPair(pairs, key, occupant, capacityWord);
            }
        }
        if (void* const b = At<void*>(r, pole::kObject28)) {
            if (void* const a = At<void*>(r, pole::kObject24))
                Connect(self, b, a);
            if (At<uint8_t>(r, pole::kWord2C + 1)) {
                void* const key = At<void*>(r, pole::kObject28);
                void* const occupant = At<void*>(r, pole::kOccupant);
                AddRefOf(key);
                AddRefOf(At<void*>(r, pole::kOccupant));
                AppendPair(pairs, key, occupant, capacityWord);
            }
        }
        if (!At<uint8_t>(self, field::kReplacing) && At<void*>(r, pole::kObject24) && At<void*>(r, pole::kObject28)) {
            LinkLast(At<char*>(self, power::kWires), power::kWireHeadSize, r + pole::kObject24, 12);
        } else {
            if (void* const a = At<void*>(r, pole::kObject24))
                ReleaseOf(a);
            if (void* const b = At<void*>(r, pole::kObject28))
                ReleaseOf(b);
        }
        At<void*>(r, pole::kObject24) = nullptr;
        At<void*>(r, pole::kObject28) = nullptr;

        const int32_t wires = Virtual<int32_t>(At<void*>(r, pole::kOccupant), kWireCount);
        char* const linksEnd = At<char*>(r, pole::kLinksEnd);
        for (char* l = At<char*>(r, pole::kLinks); l != linksEnd; l += 8) {
            char* const other = VectorAt(self, power::kPoles).begin + At<uint32_t>(l, 0) * pole::kSize;
            bool wired = false;
            for (int32_t w = 0; w < wires; ++w)
                if (Virtual<void*>(At<void*>(r, pole::kOccupant), kWireAt, w) == At<void*>(other, pole::kOccupant)) {
                    wired = true;
                    break;
                }
            if (wired || !At<uint8_t>(self, field::kReplacing))
                continue;
            char* lower = nullptr;  // the run given to this record's pole
            char* higher = nullptr; // the run given to the other's
            const int32_t index = At<int32_t>(l, 4);
            if (index >= 0) {
                const uint32_t rz = At<uint32_t>(r, pole::kZ), oz = At<uint32_t>(other, pole::kZ);
                const bool before = rz < oz || (rz == oz && At<uint32_t>(r, pole::kX) < At<uint32_t>(other, pole::kX));
                char* const runVector = VectorAt(self, power::kRuns).begin + index * static_cast<int32_t>(kVectorSize);
                if (before)
                    lower = runVector;
                else
                    higher = runVector;
                const int32_t n = static_cast<int32_t>(At<char*>(runVector, 4) - At<char*>(runVector, 0)) >> 4;
                if (n > 0) {
                    std::size_t at = 0;
                    int32_t left = n;
                    do {
                        char* const e = At<char*>(runVector, 0) + at;
                        if (At<void*>(e, kRunWire) == nullptr) {
                            char* line = nullptr;
                            if (char* const memory =
                                    CallCdecl<char*, uint32_t>(addr::cSC4PowerLineOccupant_Allocate, kLineSize))
                                line = CallThis<char*>(addr::cSC4PowerLineOccupant_cSC4PowerLineOccupant, memory);
                            AddRefOf(line);
                            Virtual<void>(line + 4, kPartSetup);
                            // The game pushes all of ECX; SetPreview reads its low byte.
                            Virtual<void>(line, kSetPreview,
                                          static_cast<uint32_t>(At<uint8_t>(self, field::kReplacing) == 0 ? 1 : 0));
                            const uint32_t byte = At<uint8_t>(e, kRunDiagonal);
                            if (before)
                                Virtual<void>(line, kSetConnectedPoles, At<void*>(r, pole::kOccupant),
                                              At<void*>(other, pole::kOccupant), byte);
                            else
                                Virtual<void>(line, kSetConnectedPoles, At<void*>(other, pole::kOccupant),
                                              At<void*>(r, pole::kOccupant), byte);
                            uint32_t position[3];
                            PolePosition(self, At<uint32_t>(e, 0), At<uint32_t>(e, 4), position);
                            Virtual<void>(line + 4, kPartPosition, position);
                            At<char*>(e, kRunWire) = line;
                        }
                        at += kRunSize;
                    } while (--left != 0);
                }
            }
            CallThis<void, void*, char*>(addr::cSC4PowerPoleOccupant_WireTo, At<void*>(r, pole::kOccupant),
                                         At<void*>(other, pole::kOccupant), lower);
            CallThis<void, void*, char*>(addr::cSC4PowerPoleOccupant_WireTo, At<void*>(other, pole::kOccupant),
                                         At<void*>(r, pole::kOccupant), higher);
        }
    }

    // With +0x50 the run cells of links with a word above 0 let their wires go.
    if (At<uint8_t>(self, field::kReplacing)) {
        for (char* r = VectorAt(self, power::kPoles).begin; r != recordsEnd; r += pole::kSize) {
            char* const linksEnd = At<char*>(r, pole::kLinksEnd);
            for (char* l = At<char*>(r, pole::kLinks); l != linksEnd; l += 8) {
                const int32_t index = At<int32_t>(l, 4);
                if (index <= 0)
                    continue;
                char* const runVector = VectorAt(self, power::kRuns).begin + index * static_cast<int32_t>(kVectorSize);
                const int32_t n = static_cast<int32_t>(At<char*>(runVector, 4) - At<char*>(runVector, 0)) >> 4;
                if (n <= 0)
                    continue;
                std::size_t at = 0;
                for (int32_t left = n; left != 0; --left, at += kRunSize)
                    if (void* const line = At<void*>(At<char*>(runVector, 0) + at, kRunWire)) {
                        ReleaseOf(line);
                        At<void*>(At<char*>(runVector, 0) + at, kRunWire) = nullptr;
                    }
            }
        }
    }

    // Each record lets its pole go.
    {
        char* const last = VectorAt(self, power::kPoles).end;
        for (char* r = VectorAt(self, power::kPoles).begin; r != last; r += pole::kSize) {
            if (At<uint8_t>(r, pole::kNew)) {
                char* const occupant = At<char*>(r, pole::kOccupant);
                Virtual<void>(At<void*>(self, field::kOccupants), kSetHighlight, occupant ? occupant + 8 : nullptr, 0u);
            }
            char* const occupant = At<char*>(r, pole::kOccupant);
            if (At<uint8_t>(self, field::kReplacing)) {
                Virtual<void>(occupant, kPoleShow);
            } else {
                Virtual<void>(occupant + 8, kPartHighlight, 3u, 0u);
                char* const list =
                    At<uint8_t>(r, pole::kNew) ? At<char*>(self, power::kList38C) : At<char*>(self, power::kList390);
                LinkLast(list, power::kListHeadSize, r + pole::kOccupant, 4);
                AddRefOf(At<void*>(r, pole::kOccupant));
            }
            ReleaseOf(At<void*>(r, pole::kOccupant));
            At<void*>(r, pole::kOccupant) = nullptr;
        }
    }

    // Each pair: wired directly, or (with +0x50, first pass) the drag redone
    // between the two poles' cells.
    for (char* e = pairs.begin; e != pairs.end; e += 12) {
        int32_t points[4];
        int32_t a[2], b[2];
        Virtual<void>(At<void*>(e, 0), kCellOf, &a[0], &a[1]);
        Virtual<void>(At<void*>(e, 4), kCellOf, &b[0], &b[1]);
        if (!At<uint8_t>(self, field::kReplacing)) {
            CallThis<void, void*, char*>(addr::cSC4PowerPoleOccupant_WireTo, At<void*>(e, 0), At<void*>(e, 4), nullptr);
            CallThis<void, void*, char*>(addr::cSC4PowerPoleOccupant_WireTo, At<void*>(e, 4), At<void*>(e, 0), nullptr);
        } else if (static_cast<uint8_t>(again)) {
            CallThis<void>(addr::cSC4PowerLineTool_ClearPoleRecords, self);
            Vector& runs = VectorAt(self, power::kRuns);
            // The game's erase from the first run to the end: it counts the
            // runs to move down from the end to itself, so none are assigned
            // (SC4Vector16::Assign, destination from the first, source from
            // the end) and every run's buffer is freed.
            char* p = runs.begin;
            char* source = runs.end;
            const int32_t assigned = static_cast<int32_t>(source - source) / static_cast<int32_t>(kVectorSize);
            for (int32_t k = 0; k < assigned; ++k, p += kVectorSize, source += kVectorSize)
                CallThis<void, char*>(addr::SC4Vector16_Assign, p, source);
            for (char* q = p; q != runs.end; q += kVectorSize)
                if (void* const elements = At<void*>(q, 0))
                    Free(elements);
            runs.end = p;
            points[0] = a[0];
            points[1] = a[1];
            points[2] = b[0];
            points[3] = b[1];
            Virtual<void>(self, kDrawNetworkLine, &points[0], &points[2], 0u, At<uint32_t>(self, field::kType));
            At<int32_t>(self, field::kValidSteps) = At<int32_t>(self, field::kStepCount);
            CallThis<void>(addr::cSC4PowerLineTool_PlanPoles, self);
            CallThis<void, uint32_t>(addr::cSC4PowerLineTool_PlacePoles, self, 0u);
        }
        ReleaseOf(At<void*>(e, 0));
        At<void*>(e, 0) = nullptr;
        ReleaseOf(At<void*>(e, 4));
        At<void*>(e, 4) = nullptr;
    }
    if (pairs.begin)
        Free(pairs.begin);
}

#define OPENSC4_DETOUR(address, fn) OPENSC4_NETTOOL_DETOUR(address, fn)

const Detour kPower3[] = {
    OPENSC4_DETOUR(addr::cSC4PowerLineTool_PlanPoles, PlanPoles),
    OPENSC4_DETOUR(addr::cSC4PowerLineTool_PlacePoles, PlacePoles),
};

#undef OPENSC4_DETOUR

} // namespace

bool InstallPower3() { return InstallDetours(kPower3, "power3"); }

} // namespace opensc4::nettool_sim
