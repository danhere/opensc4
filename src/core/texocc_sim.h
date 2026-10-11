// Our cSC4LotBaseTextureOccupant (build 641): the class map, every vtable
// slot by slot and the object's fields. A base texture occupant is the
// ground picture of one lot (or of one landfill cell): a list of texture
// entries, one per textured cell and layer, the box around them and a usage
// number. The lot developer makes and fills one per lot (GetLotBaseTexture-
// Occupant, SetEmptyLotBaseTexture, SetNormalLotBaseTexture:
// src/core/lot_object.cpp, lot_construct.cpp, lot_build.cpp, docs/lot.md);
// the landfill zone places one per cell (PlaceBaseTexture,
// src/core/landfillzone_cells.cpp). The rest (position in the occupant
// manager, removal counts, flags, the property holder) is the occupant
// base's, cSC4Occupant (src/core/occbase_sim.h), at the object + 4.
//
// Class map
//   class ID 0xC97F987C (GetGZCLSID 0x005E0620), occupant type 0x497F6D9D,
//   interface IID 0x097F6D98 (the class ID itself is not answered by
//   QueryInterface)
//   0x60 bytes, from the pool at 0x00B0A86C (8 KB pages at address &
//   0xFFFFE000: free list +0x8, used count +0xC; cSC4LotBaseTexture-
//   Occupant::Allocate 0x005DECF0 and, inline, the factory 0x005E0820)
//   constructor 0x005E05B0, reached by the factory's jump at 0x005E084C
//   (the class registration's pointer, pushed at 0x00460809: the framework
//   makes every base texture with it, for a save's reader and for the lot
//   developer and the landfill zone alike; it returns the object itself)
//   and called at 0x006BA511 (the landfill zone's PlaceBaseTexture) and
//   0x006C424B (the lot developer's GetLotBaseTextureOccupant), each after
//   Allocate (0x006BA503, 0x006C423D) and only when the framework gave no
//   object: the only three sites
//   deleting destructor 0x005E0860, reached only through the holder part's
//   slot 22 (thunk 0x005E0680), which the base's Release 0x005E8DF0 calls
//   at the last reference
//   code 0x005DEA60 to 0x005E11F8, interleaved with the class of its models
//   (below) and with generic container code. 0x005DEA50 before it is a jump
//   to the base's Init that two more vtables hold; 0x005E1200 after it is
//   the next class's QueryInterface (IID 0xA9E2831E).
//   No derived class: the five vtables are stored by the constructor and
//   the destructor only, and no other vtable holds one of the functions
//   replaced here.
//
// +0x00 primary interface (IID 0x097F6D98), vtable 0x00AA2E18, 12 slots
// (byte offset, stack arguments from the callee's ret N, function, owner;
// the abstract interface's vtable 0x00AA2CA4 is what the constructor stores
// first):
//    0 +00 2 QueryInterface 0x005DEA80              class
//    1 +04 0 AddRef 0x005E28F0 (to the base's 0x005E89B0 with this + 4)   base
//    2 +08 0 Release 0x005E2900 (to the base's 0x005E8DF0)                base
//    3 +0C 0 AsOccupant 0x005FD7E0 (this + 4)       shared stub
//    4 +10 0 GetUsage 0x005E0630 (+0x50, signed)    class
//    5 +14 1 SetUsage 0x005E0640                    class
//    6 +18 2 GetCellCoverage 0x005E0A00             class
//    7 +1C 3 IsCellCovered 0x005DF310               class
//    8 +20 3 SetTextureSpecification 0x005E0B20     class
//    9 +24 3 UpdateVertexAltitudes 0x005DF820       class
//   10 +28 1 LoadModels 0x005E0050                  class, model code: the game's
//   11 +2C 0 UnloadModels 0x005DEE00                class, model code: the game's
// +0x04 cISC4Occupant (the base cSC4Occupant starts here; its constructor
//   0x005E9220, destructor 0x005E8C40), vtable 0x00AA2D90, 34 slots:
//    0 +00 2 QueryInterface thunk 0x005E0650 (this - 4)   class
//    1, 2    AddRef 0x005E29B0, Release 0x005E29C0        base
//    3 +0C 0 Init 0x005DEA50 (a jump to the base's 0x005E8C90)   shared stub (0x00AA3004 and 0x00AA9C04 hold it too)
//    4 +10 0 Shutdown 0x005DEA60                          class
//    5 +14 0 IsInitialized 0x00458A40                     base
//    6 +18 0 AsPropertyHolder 0x006C2040 (this + 8)       shared stub
//    7 +1C 0 GetType 0x006BDD80 (+0x24 of the part)       shared stub
//    8 +20 1 GetPosition 0x005DED20                       class
//    9 +24 1 SetPosition 0x00648F10 (nothing)             shared stub
//   10 +28 2 GetBoundingBox 0x005DED60 (+0x34 and +0x40 of the part
//            copied)                                      shared stub (8 vtables: 0x00AA46C0, 0x00AA4890, 0x00AA4AC0, 0x00AA4C58, 0x00AA4E08, 0x00AA53A8, 0x00AA55B0)
//   11-26    the base's (0x005E8E20 ... 0x005E8C20; 16 GetHighlight
//            0x00458A50, 19 GetPlaceableObject 0x006AAC50, 21
//            SetPlaceableObject 0x0078DAF0)
//   27 +6C 1 0x005DEB40 (false)                           shared stub (74 vtables)
//   28 +70 1 0x00648F10 (nothing)                         shared stub
//   29 +74 1 SetFlag 0x005E8B70                           base
//   30 +78 1 SetAllFlags 0x005DED90                       class, model code: the game's
//   31-33    ClearFlag 0x005E8BA0, IsFlagSet 0x005E8BC0, GetFlags 0x005E8BE0   base
// +0x08 cIGZSerializable, vtable 0x00AA2D74, 6 slots: 0 QueryInterface
//   thunk 0x005E0660 (class), 1 and 2 AddRef 0x005E29E0 and Release
//   0x005E29F0 (base), 3 Write 0x005DF1E0 (1; class), 4 Read 0x005E10B0
//   (1; class), 5 GetGZCLSID 0x005E0620 (class). Write and Read get `this`
//   = object + 8.
// +0x0C the property holder (cISCPropertyHolder; 0x1C bytes with its
//   exemplar holder part), vtable 0x00AA2D10, 25 slots (0x00AA2D74 follows
//   it): 0 QueryInterface thunk 0x005E0670 (class), 1 and 2 0x005E2A10 and
//   0x005E2A20 (base), 3-21 the shared holder code (0x005FEC20 ...), 22
//   the deleting destructor thunk 0x005E0680 (1 argument; class), 23 the
//   Write thunk 0x005E06C0 and 24 the Read thunk 0x005E11F0 (both this -
//   4; class).
// +0x20 cISCExemplarPropertyHolder, vtable 0x00AA2CFC, 5 slots: 0
//   QueryInterface thunk 0x005E0690 (class), 1 and 2 0x005E06A0 and
//   0x005E06B0 (base; 33 vtables hold them), 3 SetPropertyExemplar
//   0x00458E60 (1; shared stub), 4 the getter 0x0060CE80 (shared stub).
//
// Drawing and model code, all left the game's (another thread's):
//   - of this class: LoadModels 0x005E0050 (a model per entry into the
//     placeable object), UnloadModels 0x005DEE00, the update of the models
//     0x005DF640 (called at its address by our UpdateVertexAltitudes and
//     SetTextureSpecification), SetAllFlags 0x005DED90 (loads the models
//     when the occupant becomes visible) and the texture's picture count
//     0x005E08F0 (loads the texture through the resource manager; called at
//     its address by our SetTextureSpecification);
//   - the model class (0xF0 bytes, vtable 0x00AA2C80, IID 0xA9E2831D, pool
//     0x00B0A884 with 16 KB pages; one object per entry, holding a copy of
//     the entry at +0x34): 0x005DEAB0, 0x005DEB00, the pool's page helpers
//     0x005DEB50 and 0x005DEB90, the thunks 0x005DEBF0 to 0x005DECE0,
//     0x005DEE30, 0x005DEE70, 0x005DEEA0 (texture coordinates by orientation
//     and mirror), 0x005DF020 (the texture), 0x005DF100 (render states),
//     0x005DF1C0, 0x005DF1D0, the constructor 0x005DF880, 0x005DF9A0,
//     0x005DF9E0 (the four vertices from the terrain), 0x005DFD40,
//     0x005DFD90, the deleting destructor 0x005E01A0, the draw 0x005E0200
//     and its thunk 0x005E0540.
// Generic container code in the range, left the game's (other classes call
// it): 0x005DEBB0 (fill of 16-byte records), 0x005DFDD0 and 0x005DFFE0
// (std::vector<bool> insert and push_back), 0x005E04A0 and 0x005E06D0 (a
// hash map's insert), 0x005E0700 (insert of n 16-byte records). 0x005E0550
// (a vector built from a range of entries) is only SetTextureSpecification's
// and is written into ours.
//
// What follows frames or the view: nothing this class holds in its own
// fields. The entries, the box and the usage change only through the
// setters (the lot developer and the landfill zone call them from the
// simulation), by UpdateVertexAltitudes when the terrain under the cells
// changes, and by a save read. The models hang on the base's placeable
// object (+0x34), which the view drives.
#pragma once

#include "core/game_access.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>

namespace opensc4::texocc_sim {

constexpr uint32_t kClassId = 0xC97F987C;
constexpr uint32_t kOccupantType = 0x497F6D9D;
constexpr uint32_t kIidBaseTexture = 0x097F6D98;
constexpr std::size_t kObjectSize = 0x60;

// One texture entry (16 bytes; the model class keeps a copy at its +0x34).
// Evidence: IsCellCovered 0x005DF310 and GetCellCoverage 0x005E0A00 (the
// cell, the alpha), SetTextureSpecification 0x005E0B20 (the picture), Write
// 0x005DF1E0 (the 14 bytes saved), the model's code (0x005DEEA0: the
// orientation and the mirror; 0x005E0200: the colour, the alpha, +0xC and
// the picture).
struct Entry {
    uint32_t texture;    // +0x0 the texture's instance ID
    uint8_t x;           // +0x4 the cell
    uint8_t z;           // +0x5
    uint8_t orientation; // +0x6 0 to 3
    uint8_t mirror;      // +0x7
    uint8_t colour[3];   // +0x8
    uint8_t alpha;       // +0xB 0xFF: covers the cell fully
    uint8_t byteC;       // +0xC 0xFF mostly; 1 for the landfill's, 3 seen (the draw's mode)
    uint8_t picture;     // +0xD which of the texture's pictures; 0xFF coming in: drawn at random
    uint8_t unset[2];    // +0xE never read or saved: whatever the caller's memory held
};
static_assert(sizeof(Entry) == 16, "texture entry size");

// Fields (offsets from the object's start). +0x04 to +0x37 are the occupant
// base's (evidence: its constructor 0x005E9220, Write 0x005E8CD0 and the
// slots named, which get the object + 4), +0x38 on this class's (evidence:
// the constructor 0x005E05B0, Write 0x005DF1E0 and the slots named).
namespace field {
constexpr std::size_t kOccupant = 0x4;      // base: the cISC4Occupant part
constexpr std::size_t kSerializable = 0x8;  // base: the cIGZSerializable part
constexpr std::size_t kHolder = 0xC;        // base: the property holder (vtable, count +0x10, properties +0x14 to +0x1C)
constexpr std::size_t kRefCount = 0x10;     // base: uint32 (AddRef 0x005E89B0)
constexpr std::size_t kProperties = 0x14;   // base: the holder's own properties: begin, end (+0x18), capacity; 8-byte entries {ID, property}
constexpr std::size_t kExemplarPart = 0x20; // base: the cISCExemplarPropertyHolder part
constexpr std::size_t kExemplar = 0x24;     // base: the exemplar (a base texture has none in the corpus saves)
constexpr std::size_t kType = 0x28;         // base: uint32 occupant type; the constructor sets 0x497F6D9D; saved
constexpr std::size_t kManagerBox = 0x2C;   // base: uint8[4], the occupant manager's cells (0xFF: none); saved
constexpr std::size_t kRemoval = 0x30;      // base: uint8[2] removal counts; saved
constexpr std::size_t kState = 0x32;        // base: uint8: bit 0 initialized, bit 1 set by Read, the high four bits the highlight
constexpr std::size_t kFlags = 0x33;        // base: uint8 flags (the base's 4; 1 once in the occupant manager; 2 visible); saved without 0x02 and 0x20
constexpr std::size_t kPlaceable = 0x34;    // base: the placeable object, referenced: the models hang on it
constexpr std::size_t kBoxLow = 0x38;       // float[3] (occupant slots 8 and 10; UpdateBounds 0x005DF370); zero from the constructor; saved
constexpr std::size_t kBoxHigh = 0x44;      // float[3]; zero from the constructor; saved
constexpr std::size_t kUsage = 0x50;        // int8 (slots 4, 5; read sign extended): 1 a landfill cell's, 2 a lot's; zero from the constructor; saved
// +0x51 to +0x53 are never written: the pool's memory.
constexpr std::size_t kEntries = 0x54;      // std::vector<Entry>: begin, end (+0x58), capacity (+0x5C); storage from the STL allocator 0x0090CF54; empty from the constructor; 14 bytes of each entry saved
constexpr std::size_t kEntriesEnd = 0x58;
constexpr std::size_t kEntriesCapacity = 0x5C;
} // namespace field

// Byte offsets of the primary vtable's slots, for Virtual<T>(texture, slot::k...).
namespace slot {
constexpr std::size_t kAddRef = 0x04, kRelease = 0x08, kAsOccupant = 0x0C, kGetUsage = 0x10, kSetUsage = 0x14;
constexpr std::size_t kGetCellCoverage = 0x18, kIsCellCovered = 0x1C, kSetTextureSpecification = 0x20;
constexpr std::size_t kUpdateVertexAltitudes = 0x24, kLoadModels = 0x28, kUnloadModels = 0x2C;
} // namespace slot

// Byte offsets of the occupant part's slots (the part is the object + 4).
namespace occupant {
constexpr std::size_t kIsInitialized = 0x14, kAsPropertyHolder = 0x18, kGetType = 0x1C, kGetPosition = 0x20;
constexpr std::size_t kGetBoundingBox = 0x28, kIsFlagSet = 0x80, kGetFlags = 0x84;
} // namespace occupant

// Logs a function's first call once ("texocc: first <name>"; defined in
// texocc_sim.cpp).
void FirstCall(const char* name, bool& logged);

} // namespace opensc4::texocc_sim
