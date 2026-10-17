// Our cSC4NetworkTool family's underground tools, second part (build 641;
// part u1 of docs/nettool.md, second half): slot 82 of the pipe and subway
// tools (the occupant makers), the three cdecl lookups the network manager
// forwards to (slots 9, 10, 11 of the manager), and the underground base's
// slots 78, 80, 16, 71, 76, 70, 58, 55 with the occupant placer 0x006596C0
// that slots 58 and 55 share. The same group as the first part,
// simulators.nettool.underground (src/core/nettool_under.cpp registers it;
// its install calls InstallUnderground2 for this list). Calls between the
// part's functions go to the game's addresses, so each detour stands alone
// and OPENSC4_NETTOOL_SKIP bisects them.
//
// Left as the game's: 0x00659240 (PropagateFromCell, ret 8): the 12-byte
// solved-cell record it appends to +0x98 is built on its stack with a byte
// store (the piece's rotation, slot 0x68) into a dword whose three upper
// bytes are never written, and the record is copied as dwords: those bytes
// are the game's stack leftovers, which C++ cannot repeat. 0x006588D0 (the
// network manager's underground rebuild, 2,402 bytes, 13 x87 sites) keeps
// the float +0x36C loaded on the x87 stack across a loop with calls around
// it (fld at 0x00658A69, fstp st(0) at 0x00658AB1) and is larger than the
// time box of this part allows to check line by line.
//
// Slot 80 (SetQuadTrianglesToDraw, 0x00657A30) is not drawing: it sets and
// clears three network-occupant flags (0x01000000, 0x02000000, 0x04000000;
// slots 0x2C/0x28) from the pipe base's three side bytes (its slot 0x170,
// GetSides 0x0065A6F0, which writes all three); the drawing reads them
// later. So it is translated.
//
// NAM. None of these functions is guarded or called by NAM's DLL; the cell
// infos, the lists +0x384/+0x388 and the world cache stay in the game's
// memory in the game's layout and are built through the game's helpers.
//
// Floats. The game's x87 runs at 53-bit precision; the terrain's and the
// tool's ST0 answers (terrain slot 0x30, the tool's slot 73 at +0x124) come
// back as doubles, exact. Every sum and difference is one double operation
// on them and float loads (exact), stored as floats where the game stores
// dwords. Slot 82: the cell's x and z are FILD of an int32 plus 2^32 when
// negative (the float 0x00A80AA8), i.e. the uint32 value, exact in a double,
// times the terrain's cell size (slot 0x18, stored as a float first), one
// double product rounded to float; x + size and z + size are double sums
// of the stored floats. Compares (fcom/fcomp, fnstsw, test ah,0x5 with
// jp/jnp, test ah,0x41 with jne) are written so an unordered compare takes
// the game's branch: see each function. fabs on a double is exact.
//
// Registers: every game caller of the cdecl lookups (0x00610550,
// 0x00610570, 0x00610540, 0x00653C00) and the vtable callers of the slots
// (call [reg+slot*4] in the network tool code) were scanned (scratch
// regscan.py / eaxscan.py; results in the part's report): none reads ECX or
// EDX after the call; plain detours.
//
// Calls, each count from the asm's pushes and the callee's own `ret N`:
// the terrain (+0x44) slot 0x18 none (ST0), slot 0x30 one (ret 4: a vertex;
// ST0); cSC4PipeOccupant::Allocate 0x006487D0 / cSC4SubwayOccupant::Allocate
// 0x00652FE0 cdecl one (the size 0x280 / 0x2D8); their constructors
// 0x006488F0 / 0x006530B0 one (ret 4: 1); the new occupant's slots 0x4
// none, 0x8 none, 0x28 one, 0xC one, 0x184 three, 0x17C one, 0x94 two,
// 0x100 two, 0x2C one, 0x78 two, 0x164 one, 0x168 one, 0x150 one, 0x158
// one, 0xC0 none, 0x16C one, 0x20 one, 0x88 one, 0x0 (QueryInterface) two,
// 0x5C none, 0xEC two; its +4 part's slot 0x44 two; RZGetFramework 0x008793EC
// cdecl none and the framework's slot 0x14 three; SC4::ReleaseRef 0x00411870
// none; the list allocator 0x00B207E4 slot 0xC one; SC4List::Clear2
// 0x00413E60 and SC4List::Destroy2 0x00413FE0 none; the occupant manager
// 0x00B43D0C slot 0xA0 five (list, low cell, high cell, filter, limit), the
// tool's occupant manager (+0x48) slots 0x50 two (AL), 0x54 three; the
// object's slot 0x18 none; SC4::GetPropertyUint8Vector 0x005FFB40 cdecl
// three; the free 0x0090CF63 and the STL allocator 0x0090CF54 cdecl one;
// ToolCellEntry 0x00633220 one (ret 4: a key); SC4VertexHeightSatisfier
// (+0x124) InsertEqualityConstraint 0x0065FDB0 two, AddVertex 0x0065FB60
// two, AddFixedVertex 0x00660360 two, RestrictVertex 0x0065FED0 three,
// SetPreferredRange 0x0065EBE0 three, SetPreferredHeight 0x0065EB90 two;
// the general tool's InsertRangeConstraintsForCell 0x006360C0 one,
// ClearOccupantHighlights 0x00625580 none, InsertOccupantsForSolvedCells
// 0x006258C0 three; cSC4NetworkCellInfo::Construct 0x00647850 none,
// ::CopyConstruct 0x006478A0 one, ::Assign 0x00632D50 one;
// cSC4NetworkWorldCache::FindOrInsertByValue 0x00647E10 one, ::CollectCells
// 0x00647E60 one; SC4PipeBase::FindByInterfaceAt 0x006584D0 cdecl four; the
// tool's slots 0x144 (81) none, 0x148 (82) four, 0x124 (73) one (ST0); the
// placer 0x006596C0 four.
#include "core/nettool_heights.h"
#include "core/nettool_sim.h"
#include "core/nettool_under.h"

#include "core/game.h"
#include "core/log.h"

#include <opensc4/addresses.h>

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace opensc4::nettool_sim {
namespace {

namespace under2 {
// The occupants slot 82 makes (their sizes, what Allocate is given).
inline constexpr uint32_t kPipeOccupantSize = 0x280, kSubwayOccupantSize = 0x2D8;
// The 12-float box slot 82 fills (four corners: x, height, z).
inline constexpr std::size_t kBoxFloats = 12;
// What slot 82 gives the new occupant's slot 0x100 (a word of the placement
// parameters, +0x290 + 0x9C).
inline constexpr std::size_t kParams32C = 0x32C;
// The tool's two hash sets of cell keys (+0xA4 slot 70 looks in, +0xB8
// slot 76): their bucket vectors' begin and end.
inline constexpr std::size_t kSetA4Buckets = 0xA8, kSetA4BucketsEnd = 0xAC;
inline constexpr std::size_t kSetB8Buckets = 0xBC, kSetB8BucketsEnd = 0xC0;
inline constexpr std::size_t kHashNodeKey = 0x4;
// The IIDs the lookups ask the occupants for, and the occupants' flags.
inline constexpr uint32_t kIidPipe = 0x49C05B9F, kIidPipeConnection = 0xEA0A5ACF, kIidPipeBase = 0x4A10AFC2;
inline constexpr uint32_t kStationProperty = 0xE90E25A1;
inline constexpr uint8_t kStationKind = 6;
inline constexpr uint32_t kSide1 = 0x01000000, kSide2 = 0x02000000, kSide3 = 0x04000000;
inline constexpr uint32_t kFlagPlaced = 0x80000000u, kFlag4000 = 0x4000, kFlag10000000 = 0x10000000;
inline constexpr uint32_t kFlagNetworkLot = 0x200000;
inline constexpr uint32_t kLotBias = 100000; // a network lot's own vertices (0x186A0)
} // namespace under2

// The occupant slots this part calls (byte offsets).
namespace occ_slot {
inline constexpr std::size_t kSetFlag = 0x28, kClearFlag = 0x2C, kSetBox = 0x0C, kHasFlag = 0x20;
inline constexpr std::size_t kSetCellFlags = 0x28, kGetPieceId = 0x5C;
inline constexpr std::size_t kSetPiece = 0x78, kVertexHeights = 0x88, kSetTypeWord = 0x94;
inline constexpr std::size_t kGetExemplar = 0xC0, kGetRotationAndFlip = 0xEC, kSetParams = 0x100;
inline constexpr std::size_t kSetCornerHeights = 0x150, kSetSloped = 0x158, kSet164 = 0x164, kSet168 = 0x168;
inline constexpr std::size_t kSet16C = 0x16C, kGetSides = 0x170, kSetSurfaceHeights = 0x17C;
inline constexpr std::size_t kSetInfo = 0x184, kHighlight = 0x44;
} // namespace occ_slot
namespace tool_slot {
inline constexpr std::size_t kVertexHt = 0x124, kNetworkId = 0x144, kMakeOccupant = 0x148;
} // namespace tool_slot
namespace occmgr_slot {
inline constexpr std::size_t kGetByCityCells = 0xA0;
inline constexpr std::size_t kAddHighlight = 0x50, kRemoveHighlight = 0x54;
} // namespace occmgr_slot

char* Sat(char* self) { return self + field::kSatisfier1; }
void* Terrain(char* self) { return At<void*>(self, field::kTerrain); }
double VertexAltitude(char* self, uint32_t vertex)
{
    return Virtual<double>(Terrain(self), terrain_slot::kVertexAltitude, vertex);
}
uint32_t NetworkId(char* self) { return Virtual<uint32_t>(self, tool_slot::kNetworkId); }
uint8_t FindByInterfaceAt(uint32_t x, uint32_t z, void** out, uint32_t id)
{
    return CallCdecl<uint8_t, uint32_t, uint32_t, void**, uint32_t>(addr::SC4PipeBase_FindByInterfaceAt, x, z, out,
                                                                    id);
}
float FloatAt(char* object, std::size_t offset) { return At<float>(object, offset); }

// A uint32 as the game's FILD of an int32 plus 2^32 when it is negative
// reads it: its unsigned value, exact in a double.
double Unsigned(uint32_t value) { return static_cast<double>(value); }

// 0x00648D10 / 0x00653A00 slot 82 of the pipe and subway tools (ret 0x10: a
// cell info, the occupant out pointer, a 12-float box out, a kind (unread)):
//   - the terrain's cell size (slot 0x18) stored as a float s; the box: x =
//     cell x * s, z = cell z * s (unsigned, one product each, stored as
//     floats); the corners (x, h0, z), (x, h1, z + s), (x + s, h2, z + s),
//     (x + s, h3, z) with the cell's heights +0x58..+0x64 copied as words
//     and the sums s + z, s + x of the stored floats;
//   - a new occupant (Allocate with the size; the constructor with 1 when
//     there is memory, else null, whose AddRef then faults as the game's
//     does) AddRef'd (slot 4); when it differs from the cell's occupant
//     (+0xC) it is AddRef'd again, stored there, and the old one released;
//   - its slot 0x28 with the cell's flags, slot 0xC with the box, slot 0x184
//     with (+0x370, the byte +0x374, +0x378), the four vertices' terrain
//     heights (slot 0x30, the terrain read again each time, stored as
//     floats) to slot 0x17C, slot 0x94 with (the tool's type, the cell's
//     word for that type), slot 0x100 with (+0x32C, 0);
//   - the out pointer gets the occupant. EAX what slot 0x100 left.
uint32_t MakeOccupant(char* self, char* info, void** out, float* box, uint32_t occupantSize,
                      std::uintptr_t allocate, std::uintptr_t construct)
{
    const float size = Virtual<float>(Terrain(self), 0x18);
    const double sizeD = static_cast<double>(size);
    box[0] = static_cast<float>(Unsigned(At<uint32_t>(info, cell::kX)) * sizeD);
    box[2] = static_cast<float>(Unsigned(At<uint32_t>(info, cell::kZ)) * sizeD);
    const float z1 = static_cast<float>(sizeD + static_cast<double>(box[2]));
    const float x1 = static_cast<float>(sizeD + static_cast<double>(box[0]));
    uint32_t* const words = reinterpret_cast<uint32_t*>(box);
    words[3] = words[0];
    words[1] = At<uint32_t>(info, cell::kHeights);
    box[5] = z1;
    words[4] = At<uint32_t>(info, cell::kHeights + 4);
    box[6] = x1;
    box[8] = z1;
    words[7] = At<uint32_t>(info, cell::kHeights + 8);
    box[9] = x1;
    words[11] = words[2];
    words[10] = At<uint32_t>(info, cell::kHeights + 12);

    char* occupant = nullptr;
    if (char* const memory = CallCdecl<char*, uint32_t>(allocate, occupantSize))
        occupant = CallThis<char*, uint32_t>(construct, memory, 1u);
    AddRefOf(occupant);
    void* const old = At<void*>(info, cell::kOccupant);
    if (occupant != old) {
        AddRefOf(occupant);
        At<void*>(info, cell::kOccupant) = occupant;
        if (old)
            ReleaseOf(old);
    }
    Virtual<uint32_t>(occupant, occ_slot::kSetCellFlags, At<uint32_t>(info, cell::kFlags));
    Virtual<uint32_t>(occupant, occ_slot::kSetBox, box);
    Virtual<uint32_t>(occupant, occ_slot::kSetInfo, At<uint32_t>(self, under::kInfo370),
                      static_cast<uint32_t>(At<uint8_t>(self, under::kInfo374)),
                      At<uint32_t>(self, under::kInfo378));
    float heights[4];
    for (std::size_t corner = 0; corner < 4; ++corner)
        heights[corner] = static_cast<float>(VertexAltitude(self, At<uint32_t>(info, cell::kVertices + 4 * corner)));
    Virtual<uint32_t>(occupant, occ_slot::kSetSurfaceHeights, static_cast<float*>(heights));
    const uint32_t type = At<uint32_t>(self, field::kType);
    Virtual<uint32_t>(occupant, occ_slot::kSetTypeWord, type, At<uint32_t>(info, cell::kByType + type * 4));
    const uint32_t answer =
        Virtual<uint32_t>(occupant, occ_slot::kSetParams, At<uint32_t>(self, under2::kParams32C), 0u);
    *out = occupant;
    return answer;
}
uint32_t __fastcall PipeMakeOccupant(char* self, void*, char* info, void** out, float* box, uint32_t)
{
    static bool logged = false;
    FirstCall("PipeToolMakeOccupant", logged);
    return MakeOccupant(self, info, out, box, under2::kPipeOccupantSize, addr::cSC4PipeOccupant_Allocate,
                        addr::cSC4PipeOccupant_cSC4PipeOccupant);
}
uint32_t __fastcall SubwayMakeOccupant(char* self, void*, char* info, void** out, float* box, uint32_t)
{
    static bool logged = false;
    FirstCall("SubwayToolMakeOccupant", logged);
    return MakeOccupant(self, info, out, box, under2::kSubwayOccupantSize, addr::cSC4SubwayOccupant_Allocate,
                        addr::cSC4SubwayOccupant_cSC4SubwayOccupant);
}

// 0x00648F70 / 0x00649080 (cdecl: cell x, cell z, the out pointer; the
// network manager's slots 10 and 11 forward to them): the list allocator
// cached at 0x00B207E4 (fetched from the framework the first time), a list
// head linked to itself, SC4List::Clear2 on it, the occupant manager's
// slot 0xA0 (the list, the cell twice, no filter, no limit); the out
// pointer cleared; for each occupant of the list the interface `iid`
// (QueryInterface into a cleared local): found, the out pointer's old one
// released and the new one stored, the answer 1; each occupant released;
// the list destroyed (SC4List::Destroy2). AL whether one was found.
bool FindAtCell(uint32_t x, uint32_t z, void** out, uint32_t iid)
{
    uint32_t cellXZ[2] = {x, z};
    bool found = false;
    void* const allocator = ListNodeAllocator();
    char* const head = Virtual<char*>(allocator, 0xC, 0xCu);
    At<char*>(head, 0) = head;
    At<char*>(head, 4) = head;
    char* list = head;
    CallThis<void>(addr::SC4List_Clear2, &list);
    void* const occupants = Global<void*>(addr::SC4_NetworkService);
    Virtual<uint32_t>(occupants, occmgr_slot::kGetByCityCells, &list, static_cast<uint32_t*>(cellXZ),
                      static_cast<uint32_t*>(cellXZ), 0u, 0xFFFFFFFFu);
    char* const listHead = list;
    *out = nullptr;
    for (char* node = At<char*>(listHead, 0); node != listHead; node = At<char*>(node, 0)) {
        void* const occupant = At<void*>(node, 8);
        void* asked = nullptr;
        if (Virtual<uint8_t>(occupant, 0x0, iid, &asked)) {
            if (*out)
                ReleaseOf(*out);
            *out = asked;
            found = true;
        }
        ReleaseOf(occupant);
    }
    CallThis<void>(addr::SC4List_Destroy2, &list);
    return found;
}
bool __cdecl FindPipe(uint32_t x, uint32_t z, void** out)
{
    static bool logged = false;
    FirstCall("ToolFindPipe", logged);
    return FindAtCell(x, z, out, under2::kIidPipe);
}
bool __cdecl FindPipeConnection(uint32_t x, uint32_t z, void** out)
{
    static bool logged = false;
    FirstCall("ToolFindPipeConnection", logged);
    return FindAtCell(x, z, out, under2::kIidPipeConnection);
}

// 0x00653B80 (cdecl: an occupant; the network manager's slot 9 forwards to
// it): a byte vector (three words cleared) filled from the occupant's
// property holder (slot 0x18) by SC4::GetPropertyUint8Vector with the
// property 0xE90E25A1; when that succeeds, whether a byte of it is 6 (the
// first match ends the search). The vector's memory freed (0x0090CF63)
// when there is any. AL the answer.
bool __cdecl IsSubwayStation(void* occupant)
{
    static bool logged = false;
    FirstCall("ToolIsSubwayStation", logged);
    Vector bytes{nullptr, nullptr, nullptr};
    void* const holder = Virtual<void*>(occupant, 0x18);
    const uint8_t got = CallCdecl<uint8_t, void*, uint32_t, Vector*>(addr::SC4_GetPropertyUint8Vector, holder,
                                                                      under2::kStationProperty, &bytes);
    bool station = false;
    if (got) {
        const uint32_t count = static_cast<uint32_t>(bytes.end - bytes.begin);
        for (uint32_t i = 0; i != count; ++i) {
            if (static_cast<uint8_t>(bytes.begin[i]) == under2::kStationKind) {
                station = true;
                break;
            }
        }
    }
    if (bytes.begin)
        CallCdecl<void, void*>(addr::SC4_Free, bytes.begin);
    return station;
}

// 0x00657860 slot 78 InsertNetworkSpecificConstraintsForCell (ret 4: a cell
// key): the cell's entry (0x00633220); nothing without one or when it is
// immovable (+0x53). The piece ID: the solved cell's first word (+0x98 by
// the entry's +0xB8) when +0x57 is set, else the entry's occupant's slot
// 0x5C (nothing without an occupant) followed by its slot 0xEC with two
// outs (unread). Nothing for the IDs 0, 0x200, 0x300, 0x800, 0xA00, 0x1400,
// 0x4B00 and 0x14E00. Else for the corner pairs (0, 1), (1, 2), (2, 3) (not
// (3, 0)) of the vertices +0xA8, each biased by 100000 when the entry is a
// network lot (+0x56): unless the two are equal or either is negative, the
// satisfier's InsertEqualityConstraint (first, second).
void __fastcall InsertNetworkSpecificConstraintsForCell(char* self, void*, uint32_t key)
{
    static bool logged = false;
    FirstCall("UndergroundInsertNetworkSpecificConstraintsForCell", logged);
    char* const entry = CallThis<char*, uint32_t>(addr::cSC4NetworkManager_ToolCellEntry, self, key);
    if (!entry || At<uint8_t>(entry, cell::kImmovable))
        return;
    uint32_t piece = 0;
    if (At<uint8_t>(entry, cell::kByte57)) {
        piece = At<uint32_t>(At<char*>(self, field::kSolved), At<uint32_t>(entry, cell::kSolvedIndex) * 0xC);
    } else {
        void* const occupant = At<void*>(entry, cell::kOccupant);
        if (!occupant)
            return;
        piece = Virtual<uint32_t>(occupant, occ_slot::kGetPieceId);
        uint32_t first = 0, second = 0;
        Virtual<uint32_t>(At<void*>(entry, cell::kOccupant), occ_slot::kGetRotationAndFlip, &first, &second);
    }
    switch (piece) {
    case 0:
    case 0x200:
    case 0x300:
    case 0x800:
    case 0xA00:
    case 0x1400:
    case 0x4B00:
    case 0x14E00:
        return;
    default:
        break;
    }
    const uint32_t bias = At<uint8_t>(entry, cell::kNetworkLot) ? under2::kLotBias : 0u;
    for (uint32_t corner = 0; corner < 3; ++corner) {
        const int32_t next =
            static_cast<int32_t>(At<uint32_t>(entry, cell::kVertices + 4 * ((corner + 1) & 3)) + bias);
        const int32_t here = static_cast<int32_t>(At<uint32_t>(entry, cell::kVertices + 4 * corner) + bias);
        if (here != next && here >= 0 && next >= 0)
            heights::Equality(Sat(self), static_cast<uint32_t>(here), static_cast<uint32_t>(next));
    }
}

// 0x00657A30 slot 80 SetQuadTrianglesToDraw (ret 8: an occupant, a word
// unread): the occupant's pipe base interface (0x4A10AFC2, into a cleared
// local); with it, the occupant's flags 0x01000000, 0x02000000, 0x04000000
// cleared (slot 0x2C), the base's three side bytes (slot 0x170, GetSides:
// its first byte the game keeps in its own argument slot), then 0x01000000
// set (slot 0x28) when the first is set, 0x02000000 when the second is
// clear, 0x04000000 when the third is clear. The interface released when
// there is one. EAX the release's answer, or QueryInterface's when it gave
// none (the game's last call in each path).
uint32_t __fastcall SetQuadTrianglesToDraw(char* self, void*, void* occupant, uint32_t)
{
    static bool logged = false;
    FirstCall("UndergroundSetQuadTrianglesToDraw", logged);
    (void)self;
    void* base = nullptr;
    uint32_t answer = Virtual<uint32_t>(occupant, 0x0, under2::kIidPipeBase, &base);
    if (static_cast<uint8_t>(answer)) {
        Virtual<uint32_t>(occupant, occ_slot::kClearFlag, under2::kSide1);
        Virtual<uint32_t>(occupant, occ_slot::kClearFlag, under2::kSide2);
        Virtual<uint32_t>(occupant, occ_slot::kClearFlag, under2::kSide3);
        uint8_t first = static_cast<uint8_t>(reinterpret_cast<std::uintptr_t>(occupant));
        uint8_t second = 0, third = 0;
        Virtual<void>(base, occ_slot::kGetSides, &first, &second, &third);
        if (first)
            Virtual<uint32_t>(occupant, occ_slot::kSetFlag, under2::kSide1);
        if (!second)
            Virtual<uint32_t>(occupant, occ_slot::kSetFlag, under2::kSide2);
        if (!third)
            Virtual<uint32_t>(occupant, occ_slot::kSetFlag, under2::kSide3);
    }
    if (base)
        answer = ReleaseOf(base);
    return answer;
}

// 0x00657BE0 slot 16 ClearOccupantHighlights (no arguments):
//   - the list +0x384 (its head read once for the walk): each node's object
//     (+8) has its +4 part's slot 0x44 called with (0, 0); with an occupant
//     manager (+0x48), when the object's exemplar (slot 0xC0) answers its
//     slot 0x84 with bit 0, the manager's slot 0x54 with (the +4 part, 0,
//     +0x208); then the object's slot 0x10 and its release; the nodes freed
//     (0x0090CF63, the head read again each step), the head linked to
//     itself;
//   - the list +0x388 (head read once): each object's +4 part's slot 0x44
//     with (0, 0) and the object released; the nodes freed the same way;
//   - then the general tool's ClearOccupantHighlights 0x00625580 (the game
//     jumps to it); EAX its answer.
void FreeNodes(char* self, std::size_t list)
{
    char* node = At<char*>(At<char*>(self, list), 0);
    while (node != At<char*>(self, list)) {
        char* const freed = node;
        node = At<char*>(node, 0);
        CallCdecl<void, void*>(addr::SC4_Free, freed);
    }
    At<char*>(At<char*>(self, list), 0) = At<char*>(self, list);
    At<char*>(At<char*>(self, list), 4) = At<char*>(self, list);
}
uint32_t __fastcall ClearOccupantHighlights(char* self, void*)
{
    static bool logged = false;
    FirstCall("UndergroundClearOccupantHighlights", logged);
    char* const headA = At<char*>(self, under::kListA);
    for (char* node = At<char*>(headA, 0); node != headA; node = At<char*>(node, 0)) {
        char* const object = At<char*>(node, 8);
        char* const part = object + 4;
        Virtual<uint32_t>(part, occ_slot::kHighlight, 0u, 0u);
        if (At<void*>(self, field::kOccupants)) {
            void* const exemplar = Virtual<void*>(object, occ_slot::kGetExemplar);
            if (Virtual<uint32_t>(exemplar, 0x84) & 1)
                Virtual<uint32_t>(At<void*>(self, field::kOccupants), occmgr_slot::kRemoveHighlight, part, 0u,
                                  At<uint32_t>(self, field::kWord208));
        }
        Virtual<uint32_t>(object, 0x10);
        ReleaseOf(object);
    }
    FreeNodes(self, under::kListA);
    char* const headB = At<char*>(self, under::kListB);
    for (char* node = At<char*>(headB, 0); node != headB; node = At<char*>(node, 0)) {
        char* const object = At<char*>(node, 8);
        Virtual<uint32_t>(object + 4, occ_slot::kHighlight, 0u, 0u);
        ReleaseOf(object);
    }
    FreeNodes(self, under::kListB);
    return CallThis<uint32_t>(addr::cSC4NetworkTool_ClearOccupantHighlights, self);
}

// 0x00657DA0 slot 71 CreateCellInfosForCurrentPlacementTiles (ret 4: a
// network type, unread: the tool's own +0x35C is used): for each valid step
// (signed, up to +0x78, read again after each) and each of its cells from
// the first to the last (unsigned, the last read again after each), the
// cell's entry (0x00633220: found or made). For corner i the height h is
// the terrain's altitude at the vertex (slot 0x30, ST0) less the float
// +0x36C, unrounded.
//   - An entry: the underground type flag (0x00AAA8FC by +0x35C) ORed into
//     its flags, +0x14 the step's index, +0x10 set, +0x53 set when the
//     step's kind is not 0; for each corner +0x68 h less the margin +0x2A4,
//     +0x6C h (both stored as floats), and the pair +0x88/+0x8C both the
//     lower of h and the stored height +0x58 (the game: h, as stored, when
//     h < height; the height when not, or when the compare is unordered).
//   - None: a cell info made on the stack (0x00647850): x, z, the flag,
//     +0x10 1, +0x14 the index, +0x51, +0x52, +0x54, +0x56 0, +0x53 the
//     kind not 0, +0x55 the kind is 3, the vertex indices (row stride
//     +0x248: v0 = stride * z + x, v0 + stride, v0 + stride + 1, v0 + 1);
//     for each corner +0x58 h, +0x68 h less the margin +0x2C4, +0x6C,
//     +0x8C, +0x88 h (stored as floats); put in the cache (0x00647E10 with
//     the key, 0x00632D50 with the temporary); the temporary's occupant
//     released when there is one.
void __fastcall CreateCellInfosForCurrentPlacementTiles(char* self, void*, uint32_t)
{
    static bool logged = false;
    FirstCall("UndergroundCreateCellInfosForCurrentPlacementTiles", logged);
    if (At<int32_t>(self, field::kValidSteps) <= 0)
        return;
    std::size_t offset = 0;
    for (int32_t index = 0;;) {
        uint32_t at = At<uint32_t>(At<char*>(self, field::kSteps), offset);
        if (at <= At<uint32_t>(At<char*>(self, field::kSteps), offset + step::kLastCell)) {
            for (;;) {
                char* const cells = At<char*>(self, field::kCells);
                const uint32_t z = At<uint32_t>(cells, at * kPointSize + 4);
                const uint32_t x = At<uint32_t>(cells, at * kPointSize);
                const uint32_t key = (z << 16) + x;
                char* const entry = CallThis<char*, uint32_t>(addr::cSC4NetworkManager_ToolCellEntry, self, key);
                const uint32_t type = At<uint32_t>(self, field::kType);
                const uint32_t flag = Global<uint32_t>(addr::cSC4UndergroundTool_CellTypeFlags + type * 4);
                if (entry) {
                    At<uint32_t>(entry, cell::kFlags) |= flag;
                    At<int32_t>(entry, cell::kStep) = index;
                    At<uint8_t>(entry, cell::kInPlacement) = 1;
                    if (At<uint32_t>(At<char*>(self, field::kSteps), offset + step::kIrregular) != 0)
                        At<uint8_t>(entry, cell::kImmovable) = 1;
                    for (std::size_t corner = 0; corner < 4; ++corner) {
                        const double h = VertexAltitude(self, At<uint32_t>(entry, cell::kVertices + 4 * corner)) -
                                         static_cast<double>(FloatAt(self, under::kDepth));
                        const float stored = static_cast<float>(h);
                        At<float>(entry, cell::kRanges + 8 * corner) =
                            static_cast<float>(h - static_cast<double>(FloatAt(self, field::kMarginCached)));
                        At<float>(entry, cell::kRanges + 8 * corner + 4) = stored;
                        const float height = At<float>(entry, cell::kHeights + 4 * corner);
                        // fcomp, test ah,0x5, jnp: h < height takes h; equal,
                        // greater and unordered take the height.
                        const float lower = h < static_cast<double>(height) ? stored : height;
                        At<float>(entry, cell::kRanges2 + 8 * corner + 4) = lower;
                        At<float>(entry, cell::kRanges2 + 8 * corner) = lower;
                    }
                } else {
                    alignas(4) char fresh[cell::kSize];
                    CallThis<char*>(addr::cSC4NetworkCellInfo_Construct, fresh);
                    const uint32_t kind = At<uint32_t>(At<char*>(self, field::kSteps), offset + step::kIrregular);
                    At<uint32_t>(fresh, cell::kFlags) = flag;
                    At<uint32_t>(fresh, cell::kStep) = static_cast<uint32_t>(index);
                    At<uint32_t>(fresh, cell::kX) = x;
                    At<uint32_t>(fresh, cell::kZ) = z;
                    At<uint8_t>(fresh, cell::kInPlacement) = 1;
                    At<uint8_t>(fresh, cell::kOccupied) = 0;
                    At<uint8_t>(fresh, cell::kImmovable) = kind != 0 ? 1 : 0;
                    At<uint8_t>(fresh, cell::kByte54) = 0;
                    At<uint8_t>(fresh, cell::kKind3) = kind == 3 ? 1 : 0;
                    const uint32_t stride = At<uint32_t>(self, field::kVertexStride);
                    const uint32_t v0 = stride * z + x;
                    At<uint32_t>(fresh, cell::kVertices + 4) = v0 + stride;
                    At<uint32_t>(fresh, cell::kVertices) = v0;
                    At<uint8_t>(fresh, cell::kNetworkLot) = 0;
                    At<uint8_t>(fresh, cell::kHasFlag100000) = 0;
                    At<uint32_t>(fresh, cell::kVertices + 8) = v0 + stride + 1;
                    At<uint32_t>(fresh, cell::kVertices + 12) = v0 + 1;
                    for (std::size_t corner = 0; corner < 4; ++corner) {
                        const double h = VertexAltitude(self, At<uint32_t>(fresh, cell::kVertices + 4 * corner)) -
                                         static_cast<double>(FloatAt(self, under::kDepth));
                        const float stored = static_cast<float>(h);
                        At<float>(fresh, cell::kHeights + 4 * corner) = stored;
                        At<float>(fresh, cell::kRanges + 8 * corner) =
                            static_cast<float>(h - static_cast<double>(FloatAt(self, field::kMarginNew)));
                        At<float>(fresh, cell::kRanges + 8 * corner + 4) = stored;
                        At<float>(fresh, cell::kRanges2 + 8 * corner + 4) = stored;
                        At<float>(fresh, cell::kRanges2 + 8 * corner) = stored;
                    }
                    char* const cached = CallThis<char*, uint32_t>(addr::cSC4NetworkWorldCache_FindOrInsertByValue,
                                                                   self + field::kWorldCache, key);
                    CallThis<char*, char*>(addr::cSC4NetworkCellInfo_Assign, cached, fresh);
                    if (void* const held = At<void*>(fresh, cell::kOccupant))
                        ReleaseOf(held);
                }
                ++at;
                if (at > At<uint32_t>(At<char*>(self, field::kSteps), offset + step::kLastCell))
                    break;
            }
        }
        ++index;
        offset += step::kSize;
        if (index >= At<int32_t>(self, field::kValidSteps))
            break;
    }
}

// Whether a key is in one of the tool's hash sets of keys (buckets begin
// and end at `buckets`/`end`): the bucket count (end - begin) >> 2 (signed),
// the key's bucket by unsigned remainder, the chain walked for the key at
// +4. The game divides by a zero count as ours does.
bool InKeySet(char* self, std::size_t buckets, std::size_t end, uint32_t key)
{
    char** const begin = At<char**>(self, buckets);
    const int32_t count = (At<int32_t>(self, end) - At<int32_t>(self, buckets)) >> 2;
    for (char* node = begin[key % static_cast<uint32_t>(count)]; node; node = At<char*>(node, 0))
        if (At<uint32_t>(node, under2::kHashNodeKey) == key)
            return true;
    return false;
}

// 0x00658040 slot 76 InsertRangeConstraintsForCell (ret 4: a cell key): the
// cell's entry (0x00633220); nothing without one. A network lot (+0x56):
// the general tool's InsertRangeConstraintsForCell 0x006360C0 with the key
// (the game jumps there after its call; EAX its answer). Immovable (+0x53):
// for each corner the satisfier's AddFixedVertex (vertex, height +0x58).
// Else for each corner: AddVertex (vertex, height); when the vertex is in
// the tool's set +0xB8 (a border vertex), AddFixedVertex (vertex, height)
// and nothing more for it. Else, with h the terrain's altitude (slot 0x30)
// less +0x36C (unrounded) and hf h stored as a float:
//   RestrictVertex (vertex, h < low ? hf : low, h < high ? hf : high) with
//   the pair +0x68/+0x6C (the compares on h; unordered takes the bound),
//   SetPreferredRange (vertex, hf < low2 ? hf : low2, hf < high2 ? hf :
//   high2) with the pair +0x88/+0x8C (compares on hf), and, when the cell is
//   occupied (+0x51), SetPreferredHeight (vertex, height).
uint32_t __fastcall InsertRangeConstraintsForCell(char* self, void*, uint32_t key)
{
    static bool logged = false;
    FirstCall("UndergroundInsertRangeConstraintsForCell", logged);
    char* const entry = CallThis<char*, uint32_t>(addr::cSC4NetworkManager_ToolCellEntry, self, key);
    if (!entry)
        return 0;
    if (At<uint8_t>(entry, cell::kNetworkLot))
        return CallThis<uint32_t, uint32_t>(addr::cSC4NetworkTool_InsertRangeConstraintsForCell, self, key);
    char* const sat = Sat(self);
    if (At<uint8_t>(entry, cell::kImmovable)) {
        for (std::size_t corner = 0; corner < 4; ++corner)
            heights::AddFixed(sat, At<uint32_t>(entry, cell::kVertices + 4 * corner),
                              At<uint32_t>(entry, cell::kHeights + 4 * corner));
        return 0;
    }
    for (std::size_t corner = 0; corner < 4; ++corner) {
        const uint32_t vertex = At<uint32_t>(entry, cell::kVertices + 4 * corner);
        heights::AddVertex(sat, vertex, At<uint32_t>(entry, cell::kHeights + 4 * corner));
        const uint32_t again = At<uint32_t>(entry, cell::kVertices + 4 * corner);
        if (InKeySet(self, under2::kSetB8Buckets, under2::kSetB8BucketsEnd, again)) {
            heights::AddFixed(sat, again, At<uint32_t>(entry, cell::kHeights + 4 * corner));
            continue;
        }
        const double h = VertexAltitude(self, again) - static_cast<double>(FloatAt(self, under::kDepth));
        const float hf = static_cast<float>(h);
        const float low = At<float>(entry, cell::kRanges + 8 * corner);
        const float high = At<float>(entry, cell::kRanges + 8 * corner + 4);
        // fcom, test ah,0x5, jp keeps the bound unless h < bound; the second
        // (jnp) takes h when h < bound: both pick h only when it is less.
        const float lowPick = h < static_cast<double>(low) ? hf : low;
        const float highPick = h < static_cast<double>(high) ? hf : high;
        heights::Restrict(sat, vertex, satisfier::Bits(lowPick), satisfier::Bits(highPick));
        const float high2 = At<float>(entry, cell::kRanges2 + 8 * corner + 4);
        const float low2 = At<float>(entry, cell::kRanges2 + 8 * corner);
        const float high2Pick = hf < high2 ? hf : high2;
        const float low2Pick = hf < low2 ? hf : low2;
        heights::PreferRange(sat, vertex, satisfier::Bits(low2Pick), satisfier::Bits(high2Pick));
        if (At<uint8_t>(entry, cell::kOccupied))
            heights::PreferHeight(sat, vertex, At<uint32_t>(entry, cell::kHeights + 4 * corner));
    }
    return 0;
}

// 0x00658710 slot 70 StartPlacing (no arguments): the world cache's cells
// copied into a vector of 0xBC-byte cell infos (CollectCells 0x00647E60;
// its end read once for the walk). For each that is not (+0x54 set and not
// in the placement (+0x10)) and whose key is in the tool's set +0xA4: the
// pipe base at the cell (SC4PipeBase::FindByInterfaceAt with slot 81's ID,
// into a cleared local); with one that has not the flag 0x200000 (slot
// 0x20), its four corner heights (slot 0x88) and for each vertex the
// tool's own height (slot 73 at +0x124, ST0; stored as a float) compared
// with them: a difference whose magnitude is above 0.001 (the float at
// 0x00A867A4; not when equal or unordered) marks a change. A change gives
// the occupant the tool's heights (slot 0x150) and its slot 0x158 with 1.
// The occupant is released. Then every copied cell's occupant (+0xC)
// released, the vector's memory freed when there is any.
void __fastcall StartPlacing(char* self, void*)
{
    static bool logged = false;
    FirstCall("UndergroundStartPlacing", logged);
    Vector copied{nullptr, nullptr, nullptr};
    CallThis<uint32_t, Vector*>(addr::cSC4NetworkWorldCache_CollectCells, self + field::kWorldCache, &copied);
    char* const end = copied.end;
    if (copied.begin != end) {
        for (char* info = copied.begin; info != end; info += cell::kSize) {
            if (At<uint8_t>(info, cell::kByte54) && !At<uint8_t>(info, cell::kInPlacement))
                continue;
            const uint32_t key = (At<uint32_t>(info, cell::kZ) << 16) + At<uint32_t>(info, cell::kX);
            if (!InKeySet(self, under2::kSetA4Buckets, under2::kSetA4BucketsEnd, key))
                continue;
            void* found = nullptr;
            const uint32_t id = NetworkId(self);
            FindByInterfaceAt(At<uint32_t>(info, cell::kX), At<uint32_t>(info, cell::kZ), &found, id);
            if (!found)
                continue;
            void* const occupant = found;
            bool changed = false;
            if (!Virtual<uint8_t>(occupant, occ_slot::kHasFlag, under2::kFlagNetworkLot)) {
                float have[4];
                Virtual<uint32_t>(occupant, occ_slot::kVertexHeights, static_cast<float*>(have));
                float want[4];
                for (std::size_t corner = 0; corner < 4; ++corner) {
                    const double h =
                        Virtual<double>(self, tool_slot::kVertexHt, At<uint32_t>(info, cell::kVertices + 4 * corner));
                    want[corner] = static_cast<float>(h);
                    // fcomp, test ah,0x41, jne: only a magnitude above the
                    // epsilon (not equal, not unordered) marks a change.
                    if (std::fabs(h - static_cast<double>(have[corner])) >
                        static_cast<double>(Global<float>(addr::FloraTuning_HeightEpsilon)))
                        changed = true;
                }
                if (changed) {
                    Virtual<uint32_t>(occupant, occ_slot::kSetCornerHeights, static_cast<float*>(want));
                    Virtual<uint32_t>(occupant, occ_slot::kSetSloped, 1u);
                }
            }
            ReleaseOf(occupant);
        }
        char* const last = copied.end;
        for (char* info = copied.begin; info != last; info += cell::kSize)
            if (void* const held = At<void*>(info, cell::kOccupant))
                ReleaseOf(held);
    }
    if (copied.begin)
        CallCdecl<void, void*>(addr::SC4_Free, copied.begin);
}

// 0x006596C0 (ret 0x10: a cell info, a solved-cell record, the occupant out
// pointer, a byte out; called by slots 58 and 55): the byte set to 1. The
// pipe base at the cell (FindByInterfaceAt with slot 81's ID into the out
// pointer): found, its flag 0x80000000 cleared (slot 0x2C) when the tool is
// replacing (+0x50) or set (slot 0x28) when not; AL 1. Else the tool's
// slot 82 (the cell info, the out pointer, a box on the stack, 0); no
// occupant: AL 0. Else the same flag, the record's piece (+0) and rotation
// byte (+4) to the occupant's slot 0x78, the four vertices' terrain heights
// (slot 0x30, stored as floats) to slot 0x17C, slots 0x164 and 0x168 with
// 0, the byte cleared; AL 1.
std::size_t PlacedFlagSlot(char* self)
{
    return At<uint8_t>(self, field::kReplacing) ? occ_slot::kClearFlag : occ_slot::kSetFlag;
}
bool __fastcall PlaceOccupant(char* self, void*, char* info, char* record, void** out, uint8_t* fresh)
{
    static bool logged = false;
    FirstCall("UndergroundPlaceOccupant", logged);
    *fresh = 1;
    const uint32_t id = NetworkId(self);
    if (FindByInterfaceAt(At<uint32_t>(info, cell::kX), At<uint32_t>(info, cell::kZ), out, id)) {
        Virtual<uint32_t>(*out, PlacedFlagSlot(self), under2::kFlagPlaced);
        return true;
    }
    float box[under2::kBoxFloats];
    Virtual<uint32_t>(self, tool_slot::kMakeOccupant, info, out, static_cast<float*>(box), 0u);
    if (!*out)
        return false;
    Virtual<uint32_t>(*out, PlacedFlagSlot(self), under2::kFlagPlaced);
    Virtual<uint32_t>(*out, occ_slot::kSetPiece, At<uint32_t>(record, 0),
                      static_cast<uint32_t>(At<uint8_t>(record, 4)));
    float heights[4];
    for (std::size_t corner = 0; corner < 4; ++corner)
        heights[corner] = static_cast<float>(VertexAltitude(self, At<uint32_t>(info, cell::kVertices + 4 * corner)));
    Virtual<uint32_t>(*out, occ_slot::kSetSurfaceHeights, static_cast<float*>(heights));
    Virtual<uint32_t>(*out, occ_slot::kSet164, 0u);
    Virtual<uint32_t>(*out, occ_slot::kSet168, 0u);
    *fresh = 0;
    return true;
}

// Appends an object to one of the lists +0x384/+0x388 (the head at `list`):
// a node from the STL allocator, its value the object (no reference taken:
// the list owns the one the caller has), linked last.
void Append(char* self, std::size_t list, void* object)
{
    char* const head = At<char*>(self, list);
    char* const node = CallCdecl<char*, uint32_t>(addr::SC4_stl_allocate, 0xCu);
    At<void*>(node, 8) = object;
    char* const tail = At<char*>(head, 4);
    At<char*>(node, 4) = tail;
    At<char*>(node, 0) = head;
    At<char*>(tail, 0) = node;
    At<char*>(head, 4) = node;
}

// The occupant manager's highlight (+0x48 slot 0x50 with the occupant's +4
// part, or null, and +0x208) into the cell entry's +0x54.
void Highlight(char* self, char* entry, char* occupant)
{
    char* const part = occupant ? occupant + 4 : nullptr;
    At<uint8_t>(entry, cell::kByte54) = Virtual<uint8_t>(At<void*>(self, field::kOccupants),
                                                         occmgr_slot::kAddHighlight, part,
                                                         At<uint32_t>(self, field::kWord208));
}

// 0x006597E0 slot 58 AddCellPreview (ret 8: a cell info, a solved-cell
// record): nothing unless the cell is in the placement (+0x10). A copy of
// the cell info on the stack (0x006478A0); for each corner the terrain's
// altitude (slot 0x30, kept as a float the game never reads) less +0x36C
// (unrounded, stored as a float) as the copy's height +0x58. The pipe base
// at the cell (FindByInterfaceAt, slot 81's ID): found, it goes on the list
// +0x388. Else the placer 0x006596C0 (the copy, the record, the occupant,
// the byte); AL 0: nothing more. Else the occupant's exemplar (slot 0xC0)
// gets slot 0x7C with 1, the occupant slot 0x16C with 1; when the byte is
// clear and there is an occupant manager, the highlight into the ORIGINAL
// cell info's +0x54; the occupant goes on the list +0x384. Then the
// occupant's +4 part's slot 0x44 with (1, 0) when there is one, and the
// copy's occupant released when there is one.
void __fastcall AddCellPreview(char* self, void*, char* info, char* record)
{
    static bool logged = false;
    FirstCall("UndergroundAddCellPreview", logged);
    if (!At<uint8_t>(info, cell::kInPlacement))
        return;
    alignas(4) char copy[cell::kSize];
    CallThis<char*, char*>(addr::cSC4NetworkCellInfo_CopyConstruct, copy, info);
    float altitudes[4];
    for (std::size_t corner = 0; corner < 4; ++corner) {
        const double altitude = VertexAltitude(self, At<uint32_t>(info, cell::kVertices + 4 * corner));
        altitudes[corner] = static_cast<float>(altitude);
        At<float>(copy, cell::kHeights + 4 * corner) =
            static_cast<float>(altitude - static_cast<double>(FloatAt(self, under::kDepth)));
    }
    (void)altitudes;
    char* occupant = nullptr;
    const uint32_t id = NetworkId(self);
    if (FindByInterfaceAt(At<uint32_t>(info, cell::kX), At<uint32_t>(info, cell::kZ),
                          reinterpret_cast<void**>(&occupant), id)) {
        Append(self, under::kListB, occupant);
    } else {
        uint8_t made = 0;
        if (CallThis<uint8_t, char*, char*, void**, uint8_t*>(addr::cSC4UndergroundTool_PlaceOccupant, self, copy,
                                                             record, reinterpret_cast<void**>(&occupant), &made)) {
            void* const exemplar = Virtual<void*>(occupant, occ_slot::kGetExemplar);
            Virtual<uint32_t>(exemplar, 0x7C, 1u);
            Virtual<uint32_t>(occupant, occ_slot::kSet16C, 1u);
            if (!made && At<void*>(self, field::kOccupants))
                Highlight(self, info, occupant);
            Append(self, under::kListA, occupant);
        }
    }
    if (occupant)
        Virtual<uint32_t>(occupant + 4, occ_slot::kHighlight, 1u, 0u);
    if (void* const held = At<void*>(copy, cell::kOccupant))
        ReleaseOf(held);
}

// 0x006599D0 slot 55 InsertOccupantsForSolvedCells (ret 0xC: the solved
// cells' vector, a word, a byte passed as a word): the general tool's
// 0x006258C0 with the three (the word whole). Nothing more unless the tool is replacing (+0x50) or the byte is
// set. Else for each 12-byte record (the vector's bounds read once): the
// cell's entry by the record's key (+8; 0x00633220), the placer 0x006596C0
// (the entry, the record, a cleared occupant, the byte); with an occupant:
// the entry's heights (+0x58) to its slot 0x150, the byte to slot 0x158
// (as the game's word: the vector's address with its low byte replaced),
// slot 0x94 with (the type, the entry's word for it), flag 0x4000 cleared
// (slot 0x2C), 0x10000000 set (slot 0x28); when the byte is clear and there
// is an occupant manager, the highlight into the entry's +0x54; the
// occupant released.
void __fastcall InsertOccupantsForSolvedCells(char* self, void*, Vector* solved, uint32_t word, uint32_t flag)
{
    static bool logged = false;
    FirstCall("UndergroundInsertOccupantsForSolvedCells", logged);
    CallThis<uint32_t, Vector*, uint32_t, uint32_t>(addr::cSC4NetworkTool_InsertOccupantsForSolvedCells, self, solved,
                                                    word, flag);
    if (!At<uint8_t>(self, field::kReplacing) && !static_cast<uint8_t>(flag))
        return;
    // The placer's byte out is the low byte of the game's own first argument
    // slot (the vector's address), which it hands whole to slot 0x158.
    const uint32_t slotHigh = static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(solved)) & 0xFFFFFF00u;
    char* const end = solved->end;
    for (char* record = solved->begin; record != end; record += 0xC) {
        char* const entry =
            CallThis<char*, uint32_t>(addr::cSC4NetworkManager_ToolCellEntry, self, At<uint32_t>(record, 8));
        void* occupant = nullptr;
        uint8_t made = 0;
        if (!CallThis<uint8_t, char*, char*, void**, uint8_t*>(addr::cSC4UndergroundTool_PlaceOccupant, self, entry,
                                                              record, &occupant, &made))
            continue;
        Virtual<uint32_t>(occupant, occ_slot::kSetCornerHeights, entry + cell::kHeights);
        Virtual<uint32_t>(occupant, occ_slot::kSetSloped, slotHigh | made);
        const uint32_t type = At<uint32_t>(self, field::kType);
        Virtual<uint32_t>(occupant, occ_slot::kSetTypeWord, type, At<uint32_t>(entry, cell::kByType + type * 4));
        Virtual<uint32_t>(occupant, occ_slot::kClearFlag, under2::kFlag4000);
        Virtual<uint32_t>(occupant, occ_slot::kSetFlag, under2::kFlag10000000);
        if (!made && At<void*>(self, field::kOccupants))
            Highlight(self, entry, static_cast<char*>(occupant));
        ReleaseOf(occupant);
    }
}

#define OPENSC4_DETOUR(address, fn) OPENSC4_NETTOOL_DETOUR(address, fn)

const Detour kUnderground2[] = {
    OPENSC4_DETOUR(addr::cSC4PipeTool_MakeOccupant, PipeMakeOccupant),
    OPENSC4_DETOUR(addr::cSC4SubwayTool_MakeOccupant, SubwayMakeOccupant),
    OPENSC4_DETOUR(addr::cSC4NetworkManager_ToolFindPipe, FindPipe),
    OPENSC4_DETOUR(addr::cSC4NetworkManager_ToolFindPipeConnection, FindPipeConnection),
    OPENSC4_DETOUR(addr::cSC4NetworkManager_ToolIsSubwayStation, IsSubwayStation),
    OPENSC4_DETOUR(addr::cSC4UndergroundTool_InsertNetworkSpecificConstraintsForCell,
                   InsertNetworkSpecificConstraintsForCell),
    OPENSC4_DETOUR(addr::cSC4UndergroundTool_SetQuadTrianglesToDraw, SetQuadTrianglesToDraw),
    OPENSC4_DETOUR(addr::cSC4UndergroundTool_ClearOccupantHighlights, ClearOccupantHighlights),
    OPENSC4_DETOUR(addr::cSC4UndergroundTool_CreateCellInfosForCurrentPlacementTiles,
                   CreateCellInfosForCurrentPlacementTiles),
    OPENSC4_DETOUR(addr::cSC4UndergroundTool_InsertRangeConstraintsForCell, InsertRangeConstraintsForCell),
    OPENSC4_DETOUR(addr::cSC4UndergroundTool_StartPlacing, StartPlacing),
    OPENSC4_DETOUR(addr::cSC4UndergroundTool_PlaceOccupant, PlaceOccupant),
    OPENSC4_DETOUR(addr::cSC4UndergroundTool_AddCellPreview, AddCellPreview),
    OPENSC4_DETOUR(addr::cSC4UndergroundTool_InsertOccupantsForSolvedCells, InsertOccupantsForSolvedCells),
};

#undef OPENSC4_DETOUR

} // namespace

bool InstallUnderground2() { return InstallDetours(kUnderground2, "underground2"); }

} // namespace opensc4::nettool_sim
