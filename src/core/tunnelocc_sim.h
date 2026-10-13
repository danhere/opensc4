// Our cSC4NetworkTunnelOccupant (build 641): the class map, every vtable by
// the slots the class owns and the object's fields. A tunnel occupant is one
// entrance of a tunnel (a portal): the pre-built-model network occupant's
// state (which is the network occupant's, src/core/netocc_sim.h, plus the
// pre-built model's own fields) and one pointer of its own, the tunnel's other
// entrance, which the network tool sets on both entrances when it builds the
// tunnel and which is saved as a reference.
//
// Class map
//   class ID 0x8A4BD52B (GetGZCLSID 0x006472D0), which is also the interface
//   ID the class answers to. The occupant type (what the occupant part's
//   GetType answers) is the network occupant's, 0xC772BF98: the tunnel
//   entrances are inside the network occupant test's walk
//   (src/core/netocc_autotest.cpp) and are told apart by the class ID.
//   0x1E0 bytes, from the pool at 0x00B0B570 (pages of 32 KB at address &
//   0xFFFF8000, 68 items each, the free list at +8 and the used count at
//   +0xC; the pool's table at 0x00AA9188: slot 1 the free 0x0061E7E0, which
//   is the pre-built class's and shared, slot 2 the page set-up 0x006471F0;
//   the static initialiser 0x00A73DE0 sets up the first page at start-up).
//   constructor 0x006476F0 (one argument: non-zero for the most derived
//   object), called by the factory 0x006477F0 (the class registration's
//   pointer, 0x00460755; it returns the object + 4) and at 0x006242CA (the
//   network tool's maker of an occupant for a class ID, after the pool
//   allocation 0x00647230, whose argument 0x1E0 is not read).
//   destructor 0x00647260 (called by the deleting destructor only), deleting
//   destructor 0x00647790 (flags; the holder part's slot 22 and the +0x38
//   part's slot 0 reach it through the thunks 0x00647370 and 0x006473B0).
//   The class's code range is 0x00647170 to 0x00647830. NOT the tunnel's:
//   0x00647830 and 0x00647850 (the network tool's cell record: the first
//   ORs the 13 words from +0x18 into +0x4C, the second clears +0xC, +0x18
//   to +0x50 and sets +0xB8 to -1; neither reads a tunnel) and 0x00647570
//   (`mov eax, [ecx + 0x1D0]; ret`, seven bytes, called for other classes'
//   objects too at 0x004ADFBA, 0x004AF83C, 0x0062AF07, 0x0063A8BC and
//   0x0071414B: a folded stub, not hooked; for a tunnel it answers the other
//   entrance).
//
// The parent class. The tunnel derives from the pre-built-model network
// occupant (class ID 0x49C1A034, vtable 0x00AA7038, 0x1D8 bytes with its
// virtual base at +0x1D4), which derives from the network occupant and is
// also the base of the bridge occupant. It is another file's; the tunnel's
// code calls it at the game's addresses (parent:: below):
//   - the constructor 0x0061E1A0 (ret 4: the most derived flag, 0 from the
//     tunnel), which calls the network occupant's constructor with 0, stores
//     its own vtables through the table at +0x3C (so, in a tunnel, its
//     virtual base vtable at +0x1DC and the shift 8 at +0x1D8, both replaced
//     by the tunnel's constructor after) and sets its fields from +0x168 to
//     +0x1C8 and the flag 0x00400000;
//   - the destructor 0x00604CA0 (none; its vtables, the objects at +0x1C8
//     and +0x1C4 released, then the network occupant's destructor);
//   - QueryInterface 0x0061D060 (ret 8), the interface's slot 4 0x0061D0D0
//     (none), the occupant part's Init 0x0061DB00 (none; this the object +
//     4), Write 0x0061D8E0 and Read 0x0061D9B0 (ret 4; this the object + 8).
//
// Vtables, as the constructor stores them (the slots the tunnel owns; the
// others are the network occupant's, the pre-built class's or shared stubs):
//   +0x00 primary, 0x00AA9308, 82 slots (the network occupant interface's 81
//     and one more):
//       0 QueryInterface 0x00647170 (ret 8)
//       4 Shutdown 0x006474F0
//     (1 and 2, AddRef and Release, are the jumps 0x006471A0 and 0x006471B0
//     to 0x0065A860 and 0x0065A870; the slots at 0x0061D040 to 0x0061F4A0,
//     such as 33, 61, 79 and 80, are the pre-built class's)
//   +0x04 cISC4Occupant, 0x00AA9280, 34 slots: 3 OccupantInit 0x006471C0; 0
//     is the thunk 0x006472E0 to QueryInterface, 4 the thunk 0x006476C0 to
//     Shutdown.
//   +0x08 cIGZSerializable, 0x00AA9264, 6 slots: 3 Write 0x00647580 and 4
//     Read 0x00647610 (both ret 4, the object + 8), 5 GetGZCLSID 0x006472D0.
//   +0x0C property holder, 0x00AA9200, 25 slots: 22 the deleting destructor
//     thunk 0x00647370, 23 and 24 the save thunks 0x006476D0 and 0x006476E0
//     (this - 4 to Write and Read).
//   +0x20 exemplar holder part, 0x00AA91E8; +0x38 the model part's thunk
//     part, 0x00AA91E4 (slot 0 the thunk 0x006473B0 to the deleting
//     destructor).
//   +0x3C the virtual base table 0x00AA9450 (-4, then the displacement
//     0x1A0; a copy of the pool's table follows it at 0x00AA9458), the
//     virtual base's vtable 0x00AA9198 (19 slots, all adjustor thunks,
//     0x006473C0 to 0x006474E0). The abstract vtable the most derived
//     constructor stores at +0x1DC first is 0x00AA1F38.
//
// Fields (object offsets). The network occupant's and the base's are in
// src/core/netocc_sim.h and src/core/occbase_sim.h.
#pragma once

#include "core/game_access.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>

namespace opensc4::tunnelocc_sim {

constexpr uint32_t kClassId = 0x8A4BD52B;
constexpr uint32_t kIidTunnel = 0x8A4BD52B;
constexpr uint32_t kIidPrebuilt = 0x49C1A034;        // the parent's IID (its class ID)
constexpr uint32_t kIidNetworkOccupant = 0xA821EF94;
constexpr uint32_t kIidSerializable = 0xE4FDA3D4;
constexpr uint32_t kOccupantType = 0xC772BF98;
constexpr std::size_t kObjectSize = 0x1E0;

namespace field {
constexpr std::size_t kOccupant = 0x4;       // base: the cISC4Occupant part
constexpr std::size_t kSerializable = 0x8;   // the cIGZSerializable part
constexpr std::size_t kHolder = 0xC;         // base: the property holder
constexpr std::size_t kRefCount = 0x10;      // base: the reference count (occupant part + 0xC)
constexpr std::size_t kExemplarPart = 0x20;  // base: the exemplar holder part
constexpr std::size_t kState = 0x32;         // base: the state byte (occupant part + 0x2E), bit 0 initialised
constexpr std::size_t kModelPart = 0x38;     // the model part's thunk part
constexpr std::size_t kVbtable = 0x3C;       // the virtual base table pointer
constexpr std::size_t kFlags = 0x13C;        // network occupant: the network flags
constexpr std::size_t kFloats178 = 0x178;    // parent: three floats, saved
constexpr std::size_t kWord19C = 0x19C;      // parent: a word, saved; non-zero: the four floats at +0x1A0 are saved
constexpr std::size_t kFloats1A0 = 0x1A0;    // parent: four floats
constexpr std::size_t kWord1B0 = 0x1B0;      // parent: a word, saved
constexpr std::size_t kByte1B4 = 0x1B4;      // parent: a byte, saved
constexpr std::size_t kParentObject1 = 0x1C4; // parent: an object, released by its slot 4 and destructor
constexpr std::size_t kParentObject2 = 0x1C8; // parent: another
constexpr std::size_t kOtherEnd = 0x1D0;     // the tunnel's own: the other entrance (a tunnel occupant), referenced
constexpr std::size_t kVbaseShift = 0x1D8;   // the virtual base's displacement word: 0 (the displacement less 0x1A0)
constexpr std::size_t kVbase = 0x1DC;        // the virtual base (4 bytes: its vtable)
} // namespace field

// The network flags the constructor changes: 0x4000 cleared (the occupant
// part's Init clears it again through slot 11), 0x10C00000 set.
constexpr uint32_t kFlagTranslucent = 0x00004000;
constexpr uint32_t kFlagsConstructed = 0x10C00000;

// Byte offsets of vtable slots, for Virtual<T>(object, slot::k...).
namespace slot {
constexpr std::size_t kAddRef = 0x4, kRelease = 0x8, kShutdown = 0x10, kClearNetworkFlag = 0x2C;
constexpr std::size_t kOccupantInit = 0xC, kIsInitialized = 0x14; // the occupant part's
constexpr std::size_t kWrite = 0xC, kRead = 0x10, kGetGZCLSID = 0x14; // the serializable part's
} // namespace slot

// The pool: the page mask, the items of a page and the used count at which
// a full page is opened again.
constexpr std::uintptr_t kPageMask = 0xFFFF8000u;
constexpr uint32_t kPageItems = 0x44;
constexpr uint32_t kReopenCount = 0x40000033u;

// The parent class's functions the tunnel's code calls, at the game's
// addresses. The entries are named in this class's spec; when the pre-built
// class's own entries name the same addresses, only these lines change.
namespace parent {
constexpr std::uintptr_t kConstruct = addr::cSC4PrebuiltNetworkOccupant_cSC4PrebuiltNetworkOccupant;
constexpr std::uintptr_t kDestroy = addr::cSC4PrebuiltNetworkOccupant_Destroy;
constexpr std::uintptr_t kQueryInterface = addr::cSC4PrebuiltNetworkOccupant_QueryInterface;
constexpr std::uintptr_t kShutdown = addr::cSC4PrebuiltNetworkOccupant_Shutdown;
constexpr std::uintptr_t kOccupantInit = addr::cSC4PrebuiltNetworkOccupant_OccupantInit;
constexpr std::uintptr_t kWrite = addr::cSC4PrebuiltNetworkOccupant_Write;
constexpr std::uintptr_t kRead = addr::cSC4PrebuiltNetworkOccupant_Read;
} // namespace parent

// Logs a function's first call once ("tunnelocc: first <name>"; defined in
// tunnelocc_sim.cpp).
void FirstCall(const char* name, bool& logged);

} // namespace opensc4::tunnelocc_sim
