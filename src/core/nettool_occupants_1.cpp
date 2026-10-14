// Our cSC4NetworkTool, part t7, first file (build 641; the class map and the
// fields are in src/core/nettool_sim.h, this part's layouts in
// nettool_occupants.h): the occupants. When the player's drag is placed, the
// segment placer 0x0063B830 (under PlaceAllSegments 0x0063BDC0) and the
// NAM-guarded 0x0062CD50 hand the solved cells to slot 55
// InsertOccupantsForSolvedCells; it makes or updates a network occupant per
// cell (slot 72 InsertNetworkOccupant, which makes it through slot 54
// CreateNetworkOccupant, a prebuilt piece when the piece has an exemplar,
// with a support pylon picked from its edges), or, while the tool only
// previews (+0x50 clear), puts the cells' heights and previews into the
// preview drawable. The second file has the tunnels, the city-edge
// connections, the RUL0 intersections' occupants, the cost (slot 20) and
// the major bridges' cell vectors. Five groups, each a list of detours at
// the game's addresses:
//   simulators.nettool.occupants (this file): slot 54 CreateNetworkOccupant
//     0x00624210, slot 55 InsertOccupantsForSolvedCells 0x006258C0, slot 72
//     InsertNetworkOccupant 0x006278A0 and their helpers:
//     VertexClearanceCategory 0x00623BB0, GetPieceExemplar 0x006244B0
//     (also called by 0x0062EEB0 and the power line tool 0x00650140),
//     HasNeighborAlongX 0x00624510, HasNeighborAlongZ 0x00624590,
//     SetUpPrebuiltOccupant 0x00624860, FindPieceRecord 0x00625860 (also
//     called by slot 56 0x00644B20), ClearCellForOccupant 0x006272F0
//     (10 detours);
//   simulators.nettool.ruleoccupants (second file): the RUL0 intersections'
//     occupants: PlaceRuleOccupants 0x0062C3A0 (called by 0x0062C4B0, which
//     the guarded 0x0062CD50 and 0x0063B830 call), PlaceRuleCellOccupant
//     0x006276D0, the rule's cons cell lookup 0x00624F00 and its table's
//     find 0x00624D20 (4 detours);
//   simulators.nettool.tunnels (second file): PlaceTunnels 0x006287D0 and
//     MakeTunnelPiece 0x00628390 (the +0xE4 records' entrances; the terrain
//     around them raised), AddEdgeConnection 0x00628BD0 (a drag that ends
//     at the city's edge: the neighbour connection records +0x100 / +0x10C)
//     (3 detours);
//   simulators.nettool.cost (second file): slot 20 GetCostOfSolution
//     0x00625640 (1 detour);
//   simulators.nettool.bridgecells (second file): the template code of the
//     major bridges' cell vectors (0x50-byte cells): Assign 0x00626750,
//     CopyRange 0x006272A0, AllocateCopy 0x0062A420, DestroyRange
//     0x0062BEF0, the vector's operator= 0x0062C1E0 (5 detours).
// The split: what makes and changes the city's occupants (the first two),
// what changes the terrain and the neighbour records (tunnels), a pure read
// (cost), template code (bridge cells); each can be tried and bisected
// alone.
//
// Every function was written from the disassembly. Calls between them go to
// the game's addresses (through the address entries; the tool's own slots
// through its vtable, as the game calls them), so each detour stands alone
// and OPENSC4_NETTOOL_SKIP bisects them.
//
// Left as the game's: nothing of the part. Under it the game's (or the
// earlier parts' or the occupant classes' own) code at its address: the
// occupants' allocators and constructors (0x00619E90/0x0061BDD0,
// 0x0061D200/0x0061E1A0, 0x00605040/0x00605160, 0x00647230/0x006476F0),
// the prebuilt occupant's SetUp 0x0061D120, UpdatePlanes 0x0061D230 and
// SetSupportPylon 0x0061D340, SetUpPieceSupports 0x00643FB0 (part t10's),
// the cell entry 0x00633220, the preview drawable's EraseCell 0x00620CF0,
// AddCell 0x00620820 and AddTexturedCell 0x00620BC0 (drawing, the game's;
// called at their addresses with a colour and a texture key picked here, as
// part t4's AddCellPreview does), FindPreviewPiece 0x00633FA0, the RUL0
// store's FindOrInsert 0x0062C2F0, SC4RuleEdges::Mirror 0x00621210, the
// tunnel's SetOtherEnd 0x00647530, the edge vector's PushBack 0x00627100,
// EdgeByte 0x00620DE0, the guarded DrawNetworkLine (slot 18, 0x0063AF40,
// through the vtable).
// Nothing here is drawing: the boxes and pylon offsets are the occupants'
// simulation data (their own code turns them into models), the preview
// calls only hand a colour and a texture key to the drawable's game code.
//
// NAM. None of these is guarded. The guarded 0x0062CD50 calls slot 55
// through the vtable (0x0062D3E1) and 0x0062C4B0 (which calls
// PlaceRuleOccupants); NAM's DLL reads the tool, the cell infos, the solved
// cells, the RUL0 rules and the occupants by layout: all stay in the game's
// memory with the game's layout, nothing is cached across a call, every
// container is grown through the game's helpers or allocators as the game
// does (SC4::stl_allocate / SC4::Free for list nodes, PushBack 0x00627100
// for the edge records, the framework's list allocator 0x00B207E4 through
// slot 3 for ClearCellForOccupant's list).
//
// Registers. Every direct game caller and every caller of the slots in the
// tool's range was scanned after the call (both branches): slot 54's
// (0x00627963, 0x00627A91, 0x006276F9, 0x006283D0, 0x0062EEE2, 0x006463E4,
// 0x00646FA6), slot 55's (0x006599E5 directly; through the vtable
// 0x0062B7B0, 0x0062D3E1 in the guarded 0x0062CD50, 0x0063BCA6), slot 72's
// (0x006250EF, 0x00625B5F, 0x00627842 is the occupant's own slot 72; NAM's
// DLL calls none of these), the helpers' (VertexClearanceCategory 0x00627996: EAX;
// GetPieceExemplar 0x0062488E, 0x0062EFBB, 0x00650183, 0x006501D9: EAX;
// HasNeighborAlongX/Z 0x00627CE5, 0x00627D48, 0x00628097, 0x006280C4: AL;
// SetUpPrebuiltOccupant 0x0062774A, 0x0062845C, 0x0062EF4C: nothing;
// FindPieceRecord 0x006278BA, 0x00644D35: EAX; ClearCellForOccupant
// 0x006277D5, 0x00627A0A, 0x00628471, 0x0062F577: nothing; the second
// file's in its top). None reads ECX or EDX before writing it; where the
// answer is a byte only AL is tested. Slot 20's callers (every `call
// [reg+0x50]` of the exe scanned for EDX read before it is written: none in
// the network code) take EAX, an int (gzcom-dll's cISC4NetworkTool::
// GetCostOfSolution). So all 23 are plain detours.
//
// Wrappers: no file of src/core/game_*.cpp or *fix*.cpp detours or tests
// return addresses in 0x00623BB0 to 0x0062C4AA. game_quiet.cpp's
// demolition wrapper replaces cSC4Demolition::DoDemolishOccupantEffect
// (slot 10, no return address test; ClearCellForOccupant calls the
// service's slot 8 0x004691F0); game_bulldoze.cpp tests return addresses
// of DemolishRegion in the bulldoze tool only. The crew: crew_life.cpp's
// FinishPieces names the tools' callers 0x0062BC6C, 0x0062D3FA, 0x0063BD15
// (not in this part); sim_timers.cpp's kCallOwners is the crew's own range
// 0x00605490 to 0x00608C50 (the occupants' constructors and this part are
// outside it and read no stopwatch). fixed_seed.cpp keys generator seeds by
// the constructing call's return address: no function here constructs a
// generator (the occupant constructors do, at their own addresses). So
// nothing needed a mark.
//
// Floats. The game runs the x87 at 53 bits, so intermediates are doubles
// where every operation is +, -, *, /, fabs or a compare and every float
// load or store rounds as `float`. None keeps a value on the x87 stack
// across a call (checked per function). The values:
//   - slot 54: the box: s = float(terrain slot 0x18), x0 = float(X * s)
//     with X the cell's x as unsigned (FILD + 2^32), z0 the same, the far
//     sides float(s + x0) and float(s + z0) (each stored twice from one
//     unrounded sum), the heights copied as bits;
//   - VertexClearanceCategory: d = h - t with t the terrain's ST0 (slot
//     0x30), compared with float 0.01 (above it only when ordered) and with
//     the tool's +0x2C8 (`test ah,5; jnp`: below it only when ordered, so a
//     NaN gives 2);
//   - slot 72's pylon: y = float((h3 + h1) * 0.5 - posY) for the rail-like
//     cases; the offsets float(A + B * 0.65) and float(-A * 0.65 + B) (or
//     B + A * 0.65 and B * 0.65 - A) with A, B = +-4; the angle float(n *
//     pi/16) from an int (FILD); the slope r = float((|z| + |x|) / |y|) of
//     two normals, the larger kept (`test ah,5; jnp`: r1 < r2 ordered takes
//     r2, else r1); y = float((alt - posY) - r * 0.65) with alt the
//     occupant's GetAltitude in ST0 at float(offX + posX), float(offZ +
//     posZ);
//   - PlaceRuleCellOccupant: (h3 + h1) < (h2 + h0) unrounded (FCOMPP, a NaN
//     not below);
//   - MakeTunnelPiece: float(f + h) for two corners; the terrain's ST0
//     compared with a corner's height (below only when ordered);
//   - GetCostOfSolution: |t - h| per vertex (t > h ? t - h : h - t, a NaN
//     takes h - t) added to a float sum and rounded each time; the cost
//     float(sum * +0x328 + 12582912.0f), whose bits less 0x4B400000 are
//     added as an integer (the game's float-to-int by the magic constant);
//   - slot 55's preview heights: slot 73's ST0 stored as floats.
//
// Stack leftovers the game stores (ours are 0):
//   - slot 72's rotation for SetUp: the low byte of a frame dword (+0x18,
//     before: the exemplar's address from the resource manager, or a float
//     of the pylon code) is set to the solved cell's rotation byte and the
//     whole dword pushed to SetUp 0x0061D120, which hands it to slot 74
//     (0x00604DF0: `mov al, [esp+4]`, a byte store). Verdict: harmless.
//   - slot 55's flip: the low byte of a frame dword (+0x28) is the cons
//     cell's flip bit, handed to FindPreviewPiece (a byte in and out) and
//     pushed whole to AddTexturedCell, whose record maker 0x0061FE60 reads
//     only the byte (part t4's AddCellPreview has the same). Harmless.
//   - slot 55's usable byte: a frame dword (+0x2C) whose low byte is set to
//     0 or 1 is pushed whole to SetUsable (slot 20, 0x00604D90: `mov al,
//     [esp+4]`). Harmless.
//   - slot 55's one-way byte: a frame dword (+0x38) whose low byte is the
//     cell's one-way byte is pushed whole to SetOneWayDirections (slot 70,
//     0x0061B9F0: our netocc_state.cpp takes the byte, the game's hands the
//     dword to the script number's FromUint8, which keeps the byte).
//     Harmless.
//   - PlaceTunnels' direction: the low byte of a frame dword (+0x28) is the
//     direction (0 to 3); the dword goes to MakeTunnelPiece, which uses its
//     low byte and (-1 - dir) & 3 (the low two bits). Harmless.
//   - AddEdgeConnection's record: the 0x18-byte record on its stack has its
//     bytes +0x15 to +0x17 never written; PushBack copies the whole record
//     into +0x100 and +0x10C. Readers of those vectors' elements:
//     GetPossibleNeighborConnection 0x006241D0 (+0x8 to +0x10 and the byte
//     +0x14), 0x00626EB0 (+0x0 and +0x4), the copies and erasers. None
//     reads +0x15 to +0x17. Harmless.
// Values the game reads that its callees write first (checked, no
// leftover): slot 72's position (+0x64 to +0x6C, the occupant part's
// GetPosition 0x006193D0), the normals (GetNormal's out), the box (slot 54
// writes all twelve floats), the exemplar outs (the resource manager writes
// them when it answers true; nothing reads them otherwise),
// ClearCellForOccupant's pole cell (GetCellPair 0x00649210 writes both),
// slot 55's node edge types (0x00619110 writes both outs when it answers
// true). One store is read from the game's stack through no pointer:
// GetPieceExemplar hands the resource manager its second argument's slot as
// the out, so the answer it gives on a "found" without a write would be the
// group it was asked; ours hands a local holding that group (the same).
//
// Calls, each count from the asm's pushes for that call and the callee's own
// `ret N` (slot numbers as byte offsets):
//   - the part's own, at the game's addresses: VertexClearanceCategory one
//     (ret 4), GetPieceExemplar two (ret 8), HasNeighborAlongX/Z three (ret
//     0xC), SetUpPrebuiltOccupant five (ret 0x14), FindPieceRecord one (ret
//     4), ClearCellForOccupant three (ret 0xC), the tool's slots 0xD8 (54)
//     four, 0x120 (72) three, 0xE8 (58) two, 0x124 (73) one (ST0), 0x140
//     (80) two;
//   - the game's: SetUpPieceSupports 0x00643FB0 four (ret 0x10), the cell
//     entry one (ret 4), the allocators cdecl one, the constructors one (ret
//     4; EAX this), SetUp three (ret 0xC), UpdatePlanes none,
//     SetSupportPylon five (ret 0x14), SC4RuleEdges::Mirror one (ret 4),
//     FindPreviewPiece five (ret 0x14), AddTexturedCell eight (ret 0x20),
//     AddCell five (ret 0x14), SC4RefPtr::Reset none (EAX the pointer's
//     address), SC4List::Destroy2 none, SC4::GetNetworkManager cdecl none,
//     SC4::stl_allocate cdecl one;
//   - the network occupant (netocc_sim.h): QueryInterface two, AddRef,
//     Release, Init (0xC) one (the box), HasNetworkFlag (0x20) one,
//     SetNetworkFlag (0x28) / ClearNetworkFlag (0x2C) one, GetNetworkFlag
//     (0x30) none, GetConnectionNodeEdgeTypes (0x34) two, GetConnection-
//     PathIDOverride (0x3C) none, SetUsable (0x50) one, GetRotationAndFlip
//     (0x68) none, slot 0x78 two (the piece and its rotation), SetEdgeTypes-
//     Flag (0x94) two, AsOccupant (0xC0) none, SetPathIDOverride (0xCC)
//     one, GetAltitude (0xE0) two floats (ST0), GetNormal (0xE4) three (EAX
//     the out), SetCostOfDemolition (0x100) two (an int64), GetOneWay-
//     Directions (0x114) none, SetOneWayDirections (0x118) one; the
//     occupant part: GetType (0x1C) none, GetPosition (0x20) one, slot 0x80
//     one; the pole interface's slot 0x20 two;
//   - the terrain (+0x44): slots 0x18 none (ST0), 0x30 one (ST0); the
//     resource manager slot 0xC five; the occupant manager (+0x48 or
//     0x00B43D0C) slots 0x50 two (AL), 0x54 three (AL), 0xA0 five (AL),
//     0xC0 one; a network occupant filter's slot 0xC one (AL); the
//     demolition service 0x00B43D00 slot 0x20 seven; the network manager's
//     MapPreviewTexture (0x34) two; the list allocator's slot 0xC one.
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
// Slot 54 and the small helpers
// ====================================================================================

// 0x00624210 (slot 54, ret 0x10: a cell info, the occupant out, a box of
// four corners (x, y, z) out, a kind): the box from the cell (the terrain's
// cell size, the four heights), a new occupant of the kind (0 a network
// occupant, 0x49C1A034 prebuilt, 0x49CC1BCD bridge, 0x8A4BD52B tunnel; a
// failed allocation null; any other kind is taken as the occupant itself,
// as the game does), stored in the out and referenced. When the tool
// places (+0x50): the cell info's occupant replaced by it (the new one
// referenced, stored, the old one released) and its flags all the cell's;
// else only the cell's 0xC0000. Then Init with the box, the flag
// 0x80000000 cleared (placing) or set, UpdatePlanes for a prebuilt one, the
// flag 0x4000 set (a tunnel: 0x10000000 set and 0x4000 cleared); when
// placing, the cell's edge words for each of its types and its one-way
// byte; the demolition cost from the parameters (+0x32C, high word 0).
void __fastcall CreateNetworkOccupant(char* self, void*, char* info, void** out, uint32_t* box, uint32_t kind)
{
    static bool logged = false;
    FirstCall("CreateNetworkOccupant", logged);
    const float scale = static_cast<float>(Virtual<double>(Terrain(self), xslot::kTerrainCellSize));
    const double x = static_cast<double>(At<uint32_t>(info, cell::kX));
    box[0] = Bits(static_cast<float>(x * static_cast<double>(scale)));
    const double z = static_cast<double>(At<uint32_t>(info, cell::kZ));
    box[2] = Bits(static_cast<float>(z * static_cast<double>(scale)));
    box[1] = At<uint32_t>(info, cell::kHeights);
    box[3] = box[0];
    const uint32_t farZ = Bits(static_cast<float>(static_cast<double>(scale) + static_cast<double>(Float(box[2]))));
    box[5] = farZ;
    box[4] = At<uint32_t>(info, cell::kHeights + 4);
    const uint32_t farX = Bits(static_cast<float>(static_cast<double>(scale) + static_cast<double>(Float(box[0]))));
    box[6] = farX;
    box[8] = farZ;
    box[7] = At<uint32_t>(info, cell::kHeights + 8);
    box[9] = farX;
    box[11] = box[2];
    box[10] = At<uint32_t>(info, cell::kHeights + 12);
    void* occupant = nullptr;
    if (kind == kKindNetwork) {
        if (void* const memory = CallCdecl<void*, uint32_t>(addr::cSC4NetworkOccupant_Allocate, kSizeNetwork))
            occupant = CallThis<void*, uint32_t>(addr::cSC4NetworkOccupant_cSC4NetworkOccupant, memory, 1u);
    } else if (kind == kKindPrebuilt) {
        if (void* const memory = CallCdecl<void*, uint32_t>(addr::cSC4PrebuiltNetworkOccupant_Allocate, kSizePrebuilt))
            occupant = CallThis<void*, uint32_t>(addr::cSC4PrebuiltNetworkOccupant_cSC4PrebuiltNetworkOccupant, memory,
                                                 1u);
    } else if (kind == kKindBridge) {
        if (void* const memory = CallCdecl<void*, uint32_t>(addr::cSC4NetworkBridgeOccupant_Allocate, kSizeBridge))
            occupant =
                CallThis<void*, uint32_t>(addr::cSC4NetworkBridgeOccupant_cSC4NetworkBridgeOccupant, memory, 1u);
    } else if (kind == kKindTunnel) {
        if (void* const memory = CallCdecl<void*, uint32_t>(addr::cSC4NetworkTunnelOccupant_Allocate, kSizeTunnel))
            occupant =
                CallThis<void*, uint32_t>(addr::cSC4NetworkTunnelOccupant_cSC4NetworkTunnelOccupant, memory, 1u);
    } else {
        occupant = reinterpret_cast<void*>(static_cast<std::uintptr_t>(kind));
    }
    *out = occupant;
    AddRefOf(occupant);
    uint32_t flags;
    if (At<uint8_t>(self, field::kReplacing)) {
        void* const old = At<void*>(info, cell::kOccupant);
        void* const made = *out;
        if (made != old) {
            if (made)
                AddRefOf(made);
            At<void*>(info, cell::kOccupant) = made;
            if (old)
                ReleaseOf(old);
        }
        flags = At<uint32_t>(info, cell::kFlags);
    } else {
        flags = At<uint32_t>(info, cell::kFlags) & oflag::kStepKindMask;
    }
    SetFlag(occupant, flags);
    Virtual<uint8_t>(occupant, oslot::kInit, box);
    if (At<uint8_t>(self, field::kReplacing))
        ClearFlag(occupant, oflag::kFlag80000000);
    else
        SetFlag(occupant, oflag::kFlag80000000);
    void* prebuilt = nullptr;
    if (Virtual<uint8_t>(occupant, oslot::kQueryInterface, kKindPrebuilt, &prebuilt))
        CallThis<void>(addr::cSC4PrebuiltNetworkOccupant_UpdatePlanes, prebuilt);
    if (kind != kKindTunnel) {
        SetFlag(occupant, oflag::kTranslucent);
    } else {
        SetFlag(occupant, oflag::kUsable);
        ClearFlag(occupant, oflag::kTranslucent);
    }
    if (At<uint8_t>(self, field::kReplacing)) {
        for (uint32_t type = 0; type < kTypes; ++type)
            if (At<uint32_t>(info, cell::kFlags) & TypeFlag(type))
                Virtual<void>(occupant, oslot::kSetEdgeTypes, type, At<uint32_t>(info, cell::kByType + type * 4));
        Virtual<void>(occupant, oslot::kSetOneWay, static_cast<uint32_t>(At<uint8_t>(info, cell::kOneWay)));
    }
    Virtual<void>(occupant, oslot::kSetDemolitionCost, At<uint32_t>(self, ofield::kDemolitionCost), 0u);
    if (prebuilt)
        ReleaseOf(prebuilt);
}

// 0x00623BB0 (ret 4: a cell info; slot 72's): for each of the four corners
// the cell's height less the terrain's at its vertex; above 0.01 it counts
// 1 when below the tool's +0x2C8, else 2. EAX the largest, 0 when none is
// above 0.01.
uint32_t __fastcall VertexClearanceCategory(char* self, void*, char* info)
{
    static bool logged = false;
    FirstCall("VertexClearanceCategory", logged);
    uint32_t largest = 0;
    for (std::size_t k = 0; k < 4; ++k) {
        const double terrain =
            Virtual<double>(Terrain(self), xslot::kTerrainVertexAltitude, At<uint32_t>(info, cell::kVertices + 4 * k));
        const double above = static_cast<double>(At<float>(info, cell::kHeights + 4 * k)) - terrain;
        // `fcom; test ah, 0x41; jne`: on only when above (ordered).
        if (!(above > static_cast<double>(Global<float>(addr::cSC4NetworkTool_ClearanceMin))))
            continue;
        // `fcomp; test ah, 5; jnp`: 1 only when below (ordered).
        const uint32_t category = above < static_cast<double>(At<float>(self, ofield::kClearance)) ? 1u : 2u;
        if (static_cast<int32_t>(category) > static_cast<int32_t>(largest))
            largest = category;
    }
    return largest;
}

// 0x006244B0 (ret 8: an instance, a group; called by 0x00624860,
// 0x0062EEB0 and the power line tool's 0x00650140): the exemplar (type
// 0x6534284A) of the group (0: the tool's row's +0xC) and the instance from
// the resource manager; EAX it when the manager answers AL, else 0. The
// game's out is its second argument's slot (still holding the group as
// passed); ours a local holding the same.
void* __fastcall GetPieceExemplar(char* self, void*, uint32_t instance, uint32_t group)
{
    static bool logged = false;
    FirstCall("GetPieceExemplar", logged);
    const uint32_t key[3] = {kExemplarType, group ? group : At<uint32_t>(ToolRow(self), orow::kGroup), instance};
    uint32_t out = group;
    const uint8_t found = GetResource(key, reinterpret_cast<void**>(&out));
    return found ? reinterpret_cast<void*>(static_cast<std::uintptr_t>(out)) : nullptr;
}

// A neighbour's cell info has a network the mask does not: its flags
// without the mask, within 0x1FFF.
bool OtherNetwork(char* info, uint32_t mask)
{
    return info && ((At<uint32_t>(info, cell::kFlags) & ~mask) & cell::kOccupantFlagMask);
}

// 0x00624510 (ret 0xC: x, z, a network mask; slot 72's): AL 1 when the cell
// at x - 1 (x above 0) or at x + 1 (x below the city's cells - 1) has a
// network outside the mask (the cell entry makes the infos), else 0.
uint8_t __fastcall HasNeighborAlongX(char* self, void*, uint32_t x, uint32_t z, uint32_t mask)
{
    static bool logged = false;
    FirstCall("HasNeighborAlongX", logged);
    if (x > 0 && OtherNetwork(Entry(self, (z << 16) + x - 1), mask))
        return 1;
    if (x < At<uint32_t>(self, field::kCellsX) - 1 && OtherNetwork(Entry(self, (z << 16) + x + 1), mask))
        return 1;
    return 0;
}

// 0x00624590 (ret 0xC: x, z, a network mask; slot 72's): the same along z.
uint8_t __fastcall HasNeighborAlongZ(char* self, void*, uint32_t x, uint32_t z, uint32_t mask)
{
    static bool logged = false;
    FirstCall("HasNeighborAlongZ", logged);
    if (z > 0 && OtherNetwork(Entry(self, ((z - 1) << 16) + x), mask))
        return 1;
    if (z < At<uint32_t>(self, field::kCellsZ) - 1 && OtherNetwork(Entry(self, ((z + 1) << 16) + x), mask))
        return 1;
    return 0;
}

// 0x00624860 (ret 0x14: an occupant, an instance, the model offset's
// address, a rotation and flip byte (as a dword), a group; called by
// 0x006276D0, 0x00628390 and 0x0062EEB0): when the occupant is prebuilt,
// its exemplar (0x006244B0 with the instance and the group) handed to
// SetUp with the offset and the rotation, and released.
void __fastcall SetUpPrebuiltOccupant(char* self, void*, void* occupant, uint32_t instance, const uint32_t* offset,
                                      uint32_t rotation, uint32_t group)
{
    static bool logged = false;
    FirstCall("SetUpPrebuiltOccupant", logged);
    void* prebuilt = nullptr;
    if (Virtual<uint8_t>(occupant, oslot::kQueryInterface, kKindPrebuilt, &prebuilt)) {
        void* const exemplar =
            CallThis<void*, uint32_t, uint32_t>(addr::cSC4NetworkTool_GetPieceExemplar, self, instance, group);
        if (exemplar) {
            CallThis<void, void*, const uint32_t*, uint32_t>(addr::cSC4PrebuiltNetworkOccupant_SetUp, prebuilt,
                                                              exemplar, offset, rotation);
            ReleaseOf(exemplar);
        }
    }
    if (prebuilt)
        ReleaseOf(prebuilt);
}

// 0x00625860 (ret 4: a piece ID; called by slot 72 and slot 56 0x00644B20):
// the value (node + 8) of the piece in the type row's hash table +0x3C, or
// 0.
char* __fastcall FindPieceRecord(char* self, void*, uint32_t piece)
{
    static bool logged = false;
    FirstCall("FindPieceRecord", logged);
    char* const node = FindNode(ToolRow(self) + orow::kPieceTable, piece);
    return node ? node + rul::store::kRule : nullptr;
}

// 0x006272F0 (ret 0xC: x, z, a network occupant filter; called by slot 72,
// 0x006276D0, 0x00628390 and 0x0062EEB0): nothing without a filter. The
// occupants of the cell (a list from the framework's cached list allocator
// 0x00B207E4, filled by the occupant manager's slot 0xA0 with the cell as
// both corners, no filter, every type): each the filter matches is taken
// out of the manager (slot 0x54: it, 1, 0); for a tool that is not the
// power line, subway or pipe one, each power pole (type 0x2890D4DE) whose
// slot 0x80 (1) says yes and whose pole interface gives this cell is
// demolished (the demolition service's slot 8: 1, the pole, 1, 0x2001, 0,
// 0, 0). The list is given back to the manager (slot 0xC0) and destroyed.
void __fastcall ClearCellForOccupant(char* self, void*, uint32_t x, uint32_t z, void* filter)
{
    static bool logged = false;
    FirstCall("ClearCellForOccupant", logged);
    if (!filter)
        return;
    ListNode* const made = Virtual<ListNode*>(ListNodeAllocator(), xslot::kListAllocate, uint32_t{sizeof(ListNode)});
    made->next = made;
    made->previous = made;
    ListNode* list = made;
    const uint32_t cellPoint[2] = {x, z};
    if (Virtual<uint8_t>(At<void*>(self, field::kOccupants), xslot::kGetByCityCells, &list, cellPoint, cellPoint, 0u,
                         0xFFFFFFFFu)) {
        for (ListNode* node = list->next; node != list; node = node->next) {
            void* const occupant = node->value;
            if (Virtual<uint8_t>(filter, xslot::kFilterMatches, occupant)) {
                Virtual<uint8_t>(At<void*>(self, field::kOccupants), xslot::kRemoveOccupant, occupant, 1u, 0u);
                continue;
            }
            const uint32_t type = At<uint32_t>(self, field::kType);
            if (type == kTypePowerLine || type == kTypeSubway || type == kTypePipe)
                continue;
            if (Virtual<uint32_t>(occupant, oslot::kGetType) != kTypePowerPole)
                continue;
            if (!Virtual<uint8_t>(occupant, oslot::kOccupantSlot80, 1u))
                continue;
            void* pole = nullptr;
            void** const where = CallThis<void**>(addr::SC4RefPtr_Reset, &pole);
            if (Virtual<uint8_t>(occupant, oslot::kQueryInterface, kIidPowerPole, where)) {
                uint32_t poleX = 0, poleZ = 0;
                Virtual<void>(pole, 0x20, &poleX, &poleZ);
                if (poleX == x && poleZ == z)
                    Virtual<uint8_t>(Global<void*>(addr::SC4_Service43D00), xslot::kDemolishOccupant, 1u, occupant,
                                     1u, 0x2001u, 0u, 0u, 0u);
            }
            if (pole)
                ReleaseOf(pole);
        }
        Virtual<void>(At<void*>(self, field::kOccupants), xslot::kReleaseOccupantList, &list);
    }
    CallThis<void>(addr::SC4List_Destroy2, &list);
}

// ====================================================================================
// Slot 72
// ====================================================================================

void SetSupportPylon(void* prebuilt, uint32_t pylon, uint32_t angle, uint32_t x, uint32_t y, uint32_t z)
{
    CallThis<void, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t>(
        addr::cSC4PrebuiltNetworkOccupant_SetSupportPylon, prebuilt, pylon, angle, x, y, z);
}

constexpr uint32_t kQuarterPiBits = 0x3F490FD0, kMinusQuarterPiBits = 0xBF490FD0;
constexpr uint32_t kFourBits = 0x40800000, kMinusFourBits = 0xC0800000, kEightBits = 0x41000000;

// Slot 72's prebuilt pylon (0x00627B14 to 0x00628252): the occupant's
// network flags (only one network: else nothing); the type of that flag (13
// when none) gives the cell's edge word and the type row's pylon IDs. The
// active sides' count and mask decide the flag 0x01000000 (a piece with a
// straight run). After UpdatePlanes, with pylon IDs (the one at the tool's
// +0xFC, else the first), the pylon's offset and angle: for a rail or
// monorail (flags 4 or 0x40) from the straight and diagonal cases, else
// from the edges (one active side, the 0x301 / 0x30100 diagonals, two
// sides' turn from the rotated edges, or 0 for the rest) with the height
// from the steeper of two normals of the piece; SetSupportPylon.
void PlacePylon(char* self, char* info, const char* solved, void* occupant, void* prebuilt, const uint32_t* box,
                const uint32_t* position)
{
    const uint32_t networks = Virtual<uint32_t>(occupant, oslot::kGetFlags) & cell::kOccupantFlagMask;
    if (networks & (networks - 1))
        return;
    uint32_t type = 0;
    while (type < kTypes && !(TypeFlag(type) & networks))
        ++type;
    const uint32_t edges = At<uint32_t>(info, cell::kByType + type * 4);
    char* const row = Row(type);
    const bool railLike = networks == 4 || networks == 0x40;
    const uint32_t active = ((edges & 0x07070707u) + 0x07070707u) & 0x08080808u;
    const uint32_t mask = (active * 0x01020408u) >> 27;
    const uint32_t count = (active * 0x01010101u) >> 27;
    if (count == 1) {
        const uint32_t low = edges & 0x03030303u;
        if (low == 0x30000 || low == 3 || low == 0x100 || low == 0x1000000)
            SetFlag(occupant, oflag::kPlanar);
    } else if (count == 2) {
        const bool straight = edges == 0x1030000 || edges == 0x103 || edges == 0x10300 || edges == 0x3000001;
        if (straight ? (mask == 3 || mask == 0xC) : (mask == 9 || mask == 6))
            SetFlag(occupant, oflag::kPlanar);
    } else if (count == 4) {
        if (edges == 0x1030103)
            SetFlag(occupant, oflag::kPlanar);
    }
    CallThis<void>(addr::cSC4PrebuiltNetworkOccupant_UpdatePlanes, prebuilt);
    const Vector& pylons = At<Vector>(row, orow::kPylons);
    if (pylons.begin == pylons.end)
        return;
    const uint32_t ids = static_cast<uint32_t>(static_cast<int32_t>(pylons.end - pylons.begin) >> 2);
    const uint32_t refresh = At<uint32_t>(self, field::kRefreshWord);
    const uint32_t pylon = ids > refresh ? At<uint32_t>(pylons.begin, refresh * 4) : At<uint32_t>(pylons.begin, 0);
    const uint32_t x = At<uint32_t>(info, cell::kX), z = At<uint32_t>(info, cell::kZ);
    const uint32_t flags = At<uint32_t>(info, cell::kFlags);
    if (railLike) {
        // The height over the piece's middle: (h3 + h1) * 0.5 - the piece's y.
        const uint32_t y = Bits(static_cast<float>(
            (static_cast<double>(At<float>(info, cell::kHeights + 12)) +
             static_cast<double>(At<float>(info, cell::kHeights + 4))) *
                static_cast<double>(Global<float>(addr::SC4_FloatHalf)) -
            static_cast<double>(Float(position[1]))));
        if (edges == 0x1030103 || edges == 0x3010301 || edges == 0x3000001 || edges == 0x103 || edges == 0x10300 ||
            edges == 0x1030000) {
            SetSupportPylon(prebuilt, pylon, kQuarterPiBits, 0u, y, 0u);
            return;
        }
        if (edges == 0x2040200 || edges == 0x40200 || edges == 0x2040000) {
            if (edges == 0x2040200 && !(z & 1) &&
                !CallThis<uint8_t, uint32_t, uint32_t, uint32_t>(addr::cSC4NetworkTool_HasNeighborAlongZ, self, x, z,
                                                                 flags))
                return;
            SetSupportPylon(prebuilt, pylon, 0u, kEightBits, y, 0u);
            return;
        }
        if (edges == 0x4020002 || edges == 0x4000002 || edges == 0x4020000) {
            if (edges == 0x4020002 && !(x & 1) &&
                !CallThis<uint8_t, uint32_t, uint32_t, uint32_t>(addr::cSC4NetworkTool_HasNeighborAlongX, self, x, z,
                                                                 flags))
                return;
            SetSupportPylon(prebuilt, pylon, 0u, 0u, y, kEightBits);
        }
        return;
    }
    const uint32_t fallback = Global<uint32_t>(addr::cSC4NetworkTool_PylonDefaultOffset);
    const bool high = (edges & 0xF8F8F8F8u) != 0;
    const bool kind2 = ((~(edges * 2u)) & edges & 0x02020202u) != 0;
    uint32_t offsetX = fallback, offsetZ = fallback, angle = 0;
    const double factor = static_cast<double>(Global<float>(addr::cSC4NetworkTool_PylonOffsetFactor));
    if (count == 1 && (edges & 0x01010101u)) {
        const double a = static_cast<double>((edges & 0xFFFF00u) ? Global<float>(addr::SC4_FloatFour)
                                                                 : Global<float>(addr::cSC4NetworkTool_PylonOffsetLow));
        const double b = static_cast<double>(Float((edges & 0xFFFF0000u) ? kFourBits : kMinusFourBits));
        double offX, offZ;
        if (!(edges & 0x02020202u)) {
            offX = a + b * factor;
            offZ = (-a) * factor + b;
        } else {
            offX = b + a * factor;
            offZ = b * factor + (-a);
        }
        offsetX = Bits(static_cast<float>(offX));
        offsetZ = Bits(static_cast<float>(offZ));
        angle = kQuarterPiBits;
    } else if (edges == 0x301) {
        angle = kMinusQuarterPiBits;
        offsetX = kMinusFourBits;
        offsetZ = kMinusFourBits;
    } else if (edges == 0x30100) {
        angle = kMinusQuarterPiBits;
        offsetX = kFourBits;
        offsetZ = kMinusFourBits;
    } else if (count == 2 && ((1u << mask) & 0x1248u) && (high || kind2)) {
        // A turn between two neighbouring sides: the edges (mirrored for a
        // flipped piece, turned by the piece's quarter turns, then until
        // two neighbouring bytes are set) give two directions in sixteenths
        // of a turn; the pylon's angle their middle.
        uint32_t e = edges;
        uint32_t s = 0, r = 0;
        const uint8_t rotation = At<uint8_t>(const_cast<char*>(solved), 4);
        if (rotation & 0x80) {
            uint32_t source = edges, mirrored = 0;
            CallThis<void, uint32_t*>(addr::SC4RuleEdges_Mirror, &source, &mirrored);
            e = mirrored;
        }
        const uint32_t turns = rotation & 3u;
        if (turns)
            e = (e >> (turns * 8)) + (e << (32 - turns * 8));
        while (!((e & 0xFFu) && (e & 0xFF00u))) {
            e = (e >> 8) + (e << 24);
            r -= 4;
            s -= 4;
        }
        const uint32_t high2 = (e >> 8) & 3u, low2 = e & 3u;
        uint32_t d1 = (r + (2u - low2) * 2u) & 0xFu;
        uint32_t d2 = (s - (high2 * 2u + 8u)) & 0xFu;
        if (e & 0xF8u) {
            if (low2 == 2)
                d1 = (e & 0x10u) ? d1 - 1 : d1 + 1;
            else
                d1 = d1 + low2 - 2;
        }
        if (e & 0xF800u) {
            if (high2 == 2)
                d2 = (e & 0x1000u) ? d2 - 1 : d2 + 1;
            else
                d2 = d2 + high2 - 2;
        }
        int32_t a1 = static_cast<int32_t>(d1), a2 = static_cast<int32_t>(d2);
        if (a1 > a2) {
            const int32_t swap = a1;
            a1 = a2;
            a2 = swap;
        }
        if (a2 - a1 > 8)
            a1 += 16;
        const int32_t sixteenths = a2 + a1 + 8;
        angle = Bits(static_cast<float>(static_cast<double>(sixteenths) *
                                        static_cast<double>(Global<float>(addr::cSC4NetworkTool_PylonAngleStep))));
        // The jump table 0x0062836C by mask - 3 (0x00628380).
        switch (mask) {
        case 3:
            offsetX = kMinusFourBits;
            offsetZ = kMinusFourBits;
            break;
        case 6:
            offsetX = kFourBits;
            offsetZ = kMinusFourBits;
            break;
        case 9:
            offsetX = kMinusFourBits;
            offsetZ = kFourBits;
            break;
        case 12:
            offsetX = kFourBits;
            offsetZ = kFourBits;
            break;
        default:
            break;
        }
    } else {
        if (static_cast<int32_t>(count) >= 3 || (count == 2 && high) || (count == 1 && kind2)) {
            // the pylon at the middle
        } else if (edges == 0x2000200) {
            if (!(z & 1) && !CallThis<uint8_t, uint32_t, uint32_t, uint32_t>(addr::cSC4NetworkTool_HasNeighborAlongZ,
                                                                             self, x, z, flags))
                return;
        } else if (edges == 0x20002) {
            if (!(x & 1) && !CallThis<uint8_t, uint32_t, uint32_t, uint32_t>(addr::cSC4NetworkTool_HasNeighborAlongX,
                                                                             self, x, z, flags))
                return;
        } else {
            return;
        }
        angle = 0;
        offsetX = 0;
        offsetZ = 0;
    }
    // The steeper of two normals of the piece (corners 0 and 2 when the
    // flag 0x01000000 is set, else 1 and 3).
    uint32_t outA[3] = {0, 0, 0}, outB[3] = {0, 0, 0};
    uint32_t first[3];
    const uint32_t* second;
    if (Virtual<uint8_t>(prebuilt, oslot::kHasFlag, oflag::kPlanar)) {
        const uint32_t* const n1 = Virtual<const uint32_t*>(prebuilt, oslot::kGetNormal, outA, box[0], box[2]);
        first[0] = n1[0];
        first[1] = n1[1];
        first[2] = n1[2];
        second = Virtual<const uint32_t*>(prebuilt, oslot::kGetNormal, outB, box[6], box[8]);
    } else {
        const uint32_t* const n1 = Virtual<const uint32_t*>(prebuilt, oslot::kGetNormal, outB, box[3], box[5]);
        first[0] = n1[0];
        first[1] = n1[1];
        first[2] = n1[2];
        second = Virtual<const uint32_t*>(prebuilt, oslot::kGetNormal, outB, box[9], box[11]);
    }
    const double n2x = static_cast<double>(Float(second[0])), n2z = static_cast<double>(Float(second[2]));
    const double n2y = static_cast<double>(Float(second[1]));
    const float slope1 = static_cast<float>(
        (std::fabs(static_cast<double>(Float(first[2]))) + std::fabs(static_cast<double>(Float(first[0])))) /
        std::fabs(static_cast<double>(Float(first[1]))));
    const float slope2 = static_cast<float>((std::fabs(n2z) + std::fabs(n2x)) / std::fabs(n2y));
    // `fcomp; test ah, 5; jnp`: the second only when the first is below it.
    const float slope = slope1 < slope2 ? slope2 : slope1;
    const uint32_t atZ =
        Bits(static_cast<float>(static_cast<double>(Float(offsetZ)) + static_cast<double>(Float(position[2]))));
    const uint32_t atX =
        Bits(static_cast<float>(static_cast<double>(Float(offsetX)) + static_cast<double>(Float(position[0]))));
    const double altitude = Virtual<double>(occupant, oslot::kGetAltitude, atX, atZ);
    const uint32_t y = Bits(static_cast<float>((altitude - static_cast<double>(Float(position[1]))) -
                                               static_cast<double>(slope) * factor));
    SetSupportPylon(prebuilt, pylon, angle, offsetX, y, offsetZ);
}

// 0x006278A0 (slot 72, ret 0xC: a cell info, its solved cell (the piece ID,
// the rotation byte), a list to append the occupant to or null; called by
// slot 55, 0x00624FD0 and 0x006276D0's caller's family through the
// vtable): the piece's record (FindPieceRecord). When one of the cell's
// networks' rows has an exemplar for the piece (the resource manager with
// the row's group): a prebuilt occupant (slot 54 with 0x49C1A034), the
// piece set (slot 0x78), its flags, its pylon (above) and its SetUp with
// the exemplar of the last group asked; else a network occupant (slot 54
// with 0) and the piece set. Then, with a record whose vertices stand
// above the terrain (VertexClearanceCategory), the piece's supports
// (0x00643FB0) and the flags 0x40000000 and 0x08000000; for a network
// occupant slot 80 with the cell's edge word of the tool's type; the flag
// 0x400000 cleared; the cell cleared for it (0x006272F0 with the tool's
// filter); the occupant into the occupant manager (+0x54 of the cell info
// its answer); the flag 0x4000 cleared when usable, else set; appended to
// the list (referenced) and released.
void __fastcall InsertNetworkOccupant(char* self, void*, char* info, const char* solved, ListNode** list)
{
    static bool logged = false;
    FirstCall("InsertNetworkOccupant", logged);
    char* const record = CallThis<char*, uint32_t>(addr::cSC4NetworkTool_FindPieceRecord, self,
                                                   At<uint32_t>(const_cast<char*>(solved), 0));
    void* const resources = Global<void*>(addr::SC4_ResourceManager);
    uint32_t key[3] = {kExemplarType, At<uint32_t>(ToolRow(self), orow::kGroup),
                       At<uint32_t>(const_cast<char*>(solved), 0)};
    bool prebuilt = false;
    void* exemplar = nullptr;
    for (uint32_t type = 0; type < kTypes; ++type) {
        if (!(At<uint32_t>(info, cell::kFlags) & TypeFlag(type)))
            continue;
        key[1] = At<uint32_t>(Row(type), orow::kGroup);
        if (Virtual<uint8_t>(resources, xslot::kGetResource, key, kExemplarIid, &exemplar, 0u, 0u)) {
            prebuilt = true;
            break;
        }
    }
    void* occupant = nullptr;
    uint32_t box[12] = {};
    if (!prebuilt) {
        Virtual<void>(self, tslot::kCreateNetworkOccupant, info, &occupant, box, kKindNetwork);
        Virtual<void>(occupant, oslot::kSetPiece, At<uint32_t>(const_cast<char*>(solved), 0),
                      static_cast<uint32_t>(At<uint8_t>(const_cast<char*>(solved), 4)));
    } else {
        ReleaseOf(exemplar);
        Virtual<void>(self, tslot::kCreateNetworkOccupant, info, &occupant, box, kKindPrebuilt);
        Virtual<void>(occupant, oslot::kSetPiece, At<uint32_t>(const_cast<char*>(solved), 0),
                      static_cast<uint32_t>(At<uint8_t>(const_cast<char*>(solved), 4)));
        ClearFlag(occupant, oflag::kFlag2000000);
        ClearFlag(occupant, oflag::kFlag4000000);
        SetFlag(occupant, oflag::kFlag8000000);
        uint32_t position[3] = {0, 0, 0};
        void* part = nullptr;
        Virtual<uint8_t>(occupant, oslot::kQueryInterface, kKindPrebuilt, &part); // the answer is not read
        Virtual<void>(reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(part) + 4), oslot::kGetPosition,
                      position);
        PlacePylon(self, info, solved, occupant, part, box, position);
        // The rotation byte: a dword of the game's frame whose upper bytes are
        // left over (see the top); SetUp's slot 74 reads the byte.
        const uint32_t rotation = At<uint8_t>(const_cast<char*>(solved), 4);
        const uint32_t piece = At<uint32_t>(const_cast<char*>(solved), 0);
        void* again = nullptr;
        if (Virtual<uint8_t>(occupant, oslot::kQueryInterface, kKindPrebuilt, &again)) {
            uint32_t group = key[1];
            if (!group)
                group = At<uint32_t>(ToolRow(self), orow::kGroup);
            const uint32_t modelKey[3] = {kExemplarType, group, piece};
            void* model = nullptr;
            if (GetResource(modelKey, &model) && model) {
                const uint32_t offset[3] = {0, 0, 0};
                CallThis<void, void*, const uint32_t*, uint32_t>(addr::cSC4PrebuiltNetworkOccupant_SetUp, again, model,
                                                                  offset, rotation);
                ReleaseOf(model);
            }
        }
        if (again)
            ReleaseOf(again);
        if (part)
            ReleaseOf(part);
    }
    if (record) {
        const uint32_t category =
            CallThis<uint32_t, char*>(addr::cSC4NetworkTool_VertexClearanceCategory, self, info);
        if (category) {
            CallThis<void, void*, char*, char*, uint32_t>(addr::cSC4NetworkTool_SetUpPieceSupports, self, occupant,
                                                          info, record, category);
            SetFlag(occupant, oflag::kSupportPylon);
            SetFlag(occupant, oflag::kFlag8000000);
        }
    }
    if (!prebuilt)
        Virtual<void>(self, tslot::kSetQuadTrianglesToDraw, occupant,
                      reinterpret_cast<char*>(reinterpret_cast<std::uintptr_t>(info) + cell::kByType +
                                              At<uint32_t>(self, field::kType) * 4));
    ClearFlag(occupant, oflag::kImmovable);
    CallThis<void, uint32_t, uint32_t, void*>(addr::cSC4NetworkTool_ClearCellForOccupant, self,
                                              At<uint32_t>(info, cell::kX), At<uint32_t>(info, cell::kZ),
                                              At<void*>(self, field::kFilter));
    if (void* const manager = At<void*>(self, field::kOccupants)) {
        const uint32_t word = At<uint32_t>(self, field::kWord208);
        void* const asOccupant = Virtual<void*>(occupant, oslot::kAsOccupant);
        At<uint8_t>(info, cell::kByte54) = Virtual<uint8_t>(manager, xslot::kInsertOccupant, asOccupant, word);
    }
    if (Virtual<uint32_t>(occupant, oslot::kGetFlags) & oflag::kUsable)
        ClearFlag(occupant, oflag::kTranslucent);
    else
        SetFlag(occupant, oflag::kTranslucent);
    if (list) {
        AddRefOf(occupant);
        PushBack(*list, occupant);
    }
    ReleaseOf(occupant);
}

// ====================================================================================
// Slot 55
// ====================================================================================

// The heights of a cell's four corners from slot 73 (a network lot's own
// vertices: + 100000), stored as floats.
void CellHeights(char* self, char* info)
{
    const uint32_t offset = At<uint8_t>(info, cell::kNetworkLot) ? kLotOffset : 0u;
    for (std::size_t k = 0; k < 4; ++k) {
        const double height =
            Virtual<double>(self, tslot::kGetNetworkVertexHt, At<uint32_t>(info, cell::kVertices + 4 * k) + offset);
        At<float>(info, cell::kHeights + 4 * k) = static_cast<float>(height);
    }
}

// Slot 55's update of an existing network lot piece (0x00625928 to
// 0x00625AFD): its edge words for each of the cell's networks (and whether
// any network's row has an exemplar for the cell's piece), the piece and
// rotation (its own when it has a path override), its flags, its path
// override (the piece, or 0 when no exemplar was found) when it has none,
// out of the occupant manager and back in, usable when its node edge types'
// second word is the last edge word set.
void UpdateNetworkLot(char* info, const char* solved, void* occupant)
{
    void* const resources = Global<void*>(addr::SC4_ResourceManager);
    uint32_t lastEdges = 0;
    bool exemplarFound = false;
    for (uint32_t type = 0; type < kTypes; ++type) {
        if (!(At<uint32_t>(info, cell::kFlags) & TypeFlag(type)))
            continue;
        const uint32_t edges = At<uint32_t>(info, cell::kByType + type * 4);
        Virtual<void>(occupant, oslot::kSetEdgeTypes, type, edges);
        lastEdges = edges;
        const uint32_t key[3] = {kExemplarType, At<uint32_t>(Row(type), orow::kGroup),
                                 At<uint32_t>(const_cast<char*>(solved), 0)};
        void* found = nullptr;
        if (Virtual<uint8_t>(resources, xslot::kGetResource, key, kExemplarIid, &found, 0u, 0u)) {
            ReleaseOf(found);
            exemplarFound = true;
        }
    }
    uint32_t nodeFirst = 0, nodeSecond = 0;
    uint8_t usable = 0;
    if (Virtual<uint8_t>(occupant, oslot::kGetNodeEdgeTypes, &nodeFirst, &nodeSecond))
        usable = lastEdges == nodeSecond ? 1 : 0;
    // The rotation: the occupant's own EAX (as the game pushes it) or the
    // solved cell's byte.
    uint32_t rotation;
    if (Virtual<uint32_t>(occupant, oslot::kGetPathOverride))
        rotation = Virtual<uint32_t>(occupant, oslot::kGetRotationAndFlip);
    else
        rotation = At<uint8_t>(const_cast<char*>(solved), 4);
    Virtual<void>(occupant, oslot::kSetPiece, At<uint32_t>(const_cast<char*>(solved), 0), rotation);
    SetFlag(occupant, oflag::kFlag8000000);
    ClearFlag(occupant, oflag::kFlag2000000);
    ClearFlag(occupant, oflag::kFlag4000000);
    if (!Virtual<uint32_t>(occupant, oslot::kGetPathOverride))
        Virtual<void>(occupant, oslot::kSetPathIdOverride,
                      exemplarFound ? At<uint32_t>(const_cast<char*>(solved), 0) : 0u);
    void* manager = Global<void*>(addr::SC4_NetworkService);
    Virtual<uint8_t>(manager, xslot::kRemoveOccupant, Virtual<void*>(occupant, oslot::kAsOccupant), 0u, 0u);
    Virtual<void>(occupant, oslot::kSetUsable, 0u);
    manager = Global<void*>(addr::SC4_NetworkService);
    Virtual<uint8_t>(manager, xslot::kInsertOccupant, Virtual<void*>(occupant, oslot::kAsOccupant), 0u);
    Virtual<void>(occupant, oslot::kSetUsable, static_cast<uint32_t>(usable));
}

// Slot 55's previews of the intersections' cells (0x00625C26 to
// 0x00625F05): the network manager asked once; for each intersection, its
// rule in the RUL0 store; for each of its cell keys with a cell info: the
// heights (slot 73), the placement mark unless the flag argument is set,
// and when the cell lies in the rule's cell grid with a cons cell, for each
// network whose edge word the cell has: the piece the rule files give it
// (FindPreviewPiece, the cons cell's turn and flip as the start) textured
// red (0xFF0000FF) with the network manager's preview texture, or a plain
// pink cell (0xFF8080FF) when none.
void PreviewIntersections(char* self, uint32_t flag)
{
    char* const end = At<char*>(self, ofield::kIntersectionsEnd);
    char* at = At<char*>(self, field::kIntersections);
    void* const manager = CallCdecl<void*>(addr::SC4_GetNetworkManager);
    for (; at != end; at += oisect::kSize) {
        char* const rule = StoreRule(At<uint32_t>(at, oisect::kRule));
        uint32_t textureKey[3] = {kTextureType, kTextureGroup, 0};
        char* const keysEnd = At<char*>(at, oisect::kCellsEnd);
        for (char* keyAt = At<char*>(at, oisect::kCells); keyAt != keysEnd; keyAt += 4) {
            char* const info = Entry(self, At<uint32_t>(keyAt, 0));
            if (!info)
                continue;
            CellHeights(self, info);
            if (!static_cast<uint8_t>(flag))
                At<uint8_t>(info, cell::kOccupied) = 1;
            const uint32_t width = Read<uint32_t>(rule, rul::rul0::kCellWidth);
            const uint32_t column = At<uint32_t>(info, cell::kX) - Read<uint32_t>(rule, rul::rul0::kMinX) -
                                    At<uint32_t>(at, oisect::kX);
            if (column >= width)
                continue;
            const uint32_t index =
                (At<uint32_t>(info, cell::kZ) - Read<uint32_t>(rule, rul::rul0::kMinZ) - At<uint32_t>(at, oisect::kZ)) *
                    width +
                column;
            char* const grid = Read<char*>(rule, rul::rul0::kCellGrid);
            if (index >= static_cast<uint32_t>(Read<char*>(rule, rul::rul0::kCellGrid + 4) - grid))
                continue;
            const uint8_t gridKey = At<uint8_t>(grid, index);
            char* const buckets = Read<char*>(rule, orule::kConsBuckets);
            const uint32_t count =
                static_cast<uint32_t>(static_cast<int32_t>(Read<char*>(rule, orule::kConsBucketsEnd) - buckets) >> 2);
            char* node = reinterpret_cast<char**>(buckets)[static_cast<uint32_t>(gridKey) % count];
            while (node && At<uint8_t>(node, rul::table::kKey) != gridKey)
                node = At<char*>(node, rul::table::kNext);
            if (!node)
                continue;
            char* const value = node + rul::store::kRule;
            if (!At<uint32_t>(info, cell::kCombined))
                continue;
            for (uint32_t type = 0; type < kTypes; ++type) {
                if (!At<uint32_t>(info, cell::kByType + type * 4))
                    continue;
                const int32_t xz[2] = {At<int32_t>(info, cell::kX), At<int32_t>(info, cell::kZ)};
                const uint8_t bits = At<uint8_t>(value, orule::kValueTurn);
                // The game's flip is a dword of its frame with only the low
                // byte written (see the top); ours is 0 above it.
                uint32_t flip = static_cast<uint32_t>(bits >> 7);
                uint8_t turn = static_cast<uint8_t>(bits & 3);
                uint32_t piece = 0;
                if (CallThis<uint8_t, const int32_t*, uint32_t, uint32_t*, uint8_t*, uint8_t*>(
                        addr::cSC4NetworkTool_FindPreviewPiece, self, xz, type, &piece, &turn,
                        reinterpret_cast<uint8_t*>(&flip))) {
                    textureKey[2] = Virtual<uint32_t>(manager, xslot::kMapPreviewTexture,
                                                      At<uint32_t>(self, field::kRefreshWord) + piece, &turn);
                    CallThis<void, const int32_t*, uint32_t, char*, uint32_t*, uint32_t, uint32_t, uint32_t, uint32_t>(
                        addr::cSC4NetworkToolPreview_AddTexturedCell, At<char*>(self, field::kPreview), xz,
                        kColourIntersection, info + cell::kHeights, textureKey, static_cast<uint32_t>(turn), flip,
                        PreviewHeight(type), 0u);
                } else {
                    CallThis<void, const int32_t*, uint32_t, char*, uint32_t, uint32_t>(
                        addr::cSC4NetworkToolPreview_AddCell, At<char*>(self, field::kPreview), xz, kColourNoPiece,
                        info + cell::kHeights, PreviewHeight(type), 0u);
                }
            }
        }
    }
}

// 0x006258C0 (slot 55, ret 0xC: a vector of solved cells, a list for the
// new occupants or null, a flag byte; called by 0x006599D0 directly and
// through the vtable by 0x0062B7B0, the guarded 0x0062CD50 and 0x0063BCA6).
// When the tool places (+0x50) and the flag is clear: for each solved cell
// with a cell info (its byte +0x51 set): no occupant, or one that is
// neither a network lot (0x200000) nor immovable (0x400000): slot 72 (the
// cell info, the solved cell, the list). A network lot: updated in place
// (above). An immovable one: on a one-way cell (flag 0x400) whose
// occupant's one-way byte differs from the cell's, the byte set and the
// occupant made unusable and usable again. Else (previewing): each solved
// cell's heights from slot 73, its +0x51 set unless the flag is, slot 58
// AddCellPreview; then the intersections' previews (above).
void __fastcall InsertOccupantsForSolvedCells(char* self, void*, Vector* solved, ListNode** list, uint32_t flag)
{
    static bool logged = false;
    FirstCall("InsertOccupantsForSolvedCells", logged);
    char* const end = solved->end;
    if (At<uint8_t>(self, field::kReplacing) && !static_cast<uint8_t>(flag)) {
        for (char* at = solved->begin; at != end; at += 12) {
            char* const info = Entry(self, At<uint32_t>(at, 8));
            if (!info)
                continue;
            void* const occupant = At<void*>(info, cell::kOccupant);
            At<uint8_t>(info, cell::kOccupied) = 1;
            if (occupant) {
                if (Virtual<uint8_t>(occupant, oslot::kHasFlag, oflag::kNetworkLot)) {
                    UpdateNetworkLot(info, at, occupant);
                    continue;
                }
                if (Virtual<uint8_t>(occupant, oslot::kHasFlag, oflag::kImmovable)) {
                    if (!((At<uint32_t>(info, cell::kFlags) >> 10) & 1u))
                        continue;
                    const uint8_t current = Virtual<uint8_t>(occupant, oslot::kGetOneWay);
                    const uint8_t wanted = At<uint8_t>(info, cell::kOneWay);
                    if (current == wanted)
                        continue;
                    Virtual<void>(occupant, oslot::kSetOneWay, static_cast<uint32_t>(wanted));
                    Virtual<void>(occupant, oslot::kSetUsable, 0u);
                    Virtual<void>(occupant, oslot::kSetUsable, 1u);
                    continue;
                }
            }
            Virtual<void>(self, tslot::kInsertNetworkOccupant, info, at, list);
        }
        return;
    }
    for (char* at = solved->begin; at != end; at += 12) {
        char* const info = Entry(self, At<uint32_t>(at, 8));
        if (!info)
            continue;
        CellHeights(self, info);
        if (!static_cast<uint8_t>(flag))
            At<uint8_t>(info, cell::kOccupied) = 1;
        Virtual<void>(self, tslot::kAddCellPreview, info, at);
    }
    PreviewIntersections(self, flag);
}

// ====================================================================================
// Installation
// ====================================================================================

#define OPENSC4_DETOUR(address, fn) OPENSC4_NETTOOL_DETOUR(address, fn)

const Detour kOccupants[] = {
    OPENSC4_DETOUR(addr::cSC4NetworkTool_CreateNetworkOccupant, CreateNetworkOccupant),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_InsertOccupantsForSolvedCells, InsertOccupantsForSolvedCells),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_InsertNetworkOccupant, InsertNetworkOccupant),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_VertexClearanceCategory, VertexClearanceCategory),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_GetPieceExemplar, GetPieceExemplar),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_HasNeighborAlongX, HasNeighborAlongX),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_HasNeighborAlongZ, HasNeighborAlongZ),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_SetUpPrebuiltOccupant, SetUpPrebuiltOccupant),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_FindPieceRecord, FindPieceRecord),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_ClearCellForOccupant, ClearCellForOccupant),
};

#undef OPENSC4_DETOUR

bool InstallOccupants(registry::Mode) { return InstallDetours(kOccupants, "occupants"); }

const bool g_registered[] = {
    registry::Register({"simulators.nettool.occupants", "simulators/nettool", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4NetworkTool_InsertNetworkOccupant,
                        "Our network tool's occupants: slot 54 CreateNetworkOccupant, slot 55 "
                        "InsertOccupantsForSolvedCells, slot 72 InsertNetworkOccupant (with the prebuilt pieces' "
                        "pylons) and their helpers: the vertex clearance, the piece exemplar, the neighbour tests, "
                        "the prebuilt set-up, FindPieceRecord and the cell clearing (10 detours; "
                        "src/core/nettool_occupants_1.cpp)",
                        false, InstallOccupants}),
};

} // namespace
} // namespace opensc4::nettool_sim
