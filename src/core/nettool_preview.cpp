// Our cSC4NetworkTool, part t4 (build 641; the class map and the fields are
// in src/core/nettool_sim.h): the tool's cell previews and occupant
// highlights, and the rule match that picks a previewed cell's piece. Three
// groups, each a list of detours at the game's addresses:
//   simulators.nettool.cellpreviews: slot 58 AddCellPreview 0x006246A0 and
//     the preview drawable's tree bookkeeping under it: AddCell 0x00620820,
//     EraseCell 0x00620CF0, the cell tree's node insert 0x006203E0, the
//     textured tree's insert 0x00620A90, node insert 0x006205C0 and node
//     maker 0x006204B0 (7 detours);
//   simulators.nettool.piecematch: the piece of a cell for a network type
//     from the type row's rule files (0x00633FA0, called by AddCellPreview
//     and slot 55), the neighbour walker's constructor 0x00627080, a rule
//     file's match 0x006220D0, an entry's match 0x00622040, the walker's
//     cached edge lookup 0x00621D30, and the check vector's InsertOverflow
//     0x00621A40, UninitializedCopy 0x00621580 and UninitializedFillN
//     0x00621550 that part r2 left the game's (8 detours);
//   simulators.nettool.highlights: slot 59 UpdateOccupantHighlights
//     0x006299A0, slot 60 DestroyHighlightedOccupants 0x00624920, the
//     collector 0x0061F7C0 (also called by 0x00609A00 and the NAM-guarded
//     0x0062CD50), the highlight set's diff 0x0061F5D0 and slot 80
//     SetQuadTrianglesToDraw 0x00623CE0 (the network occupant's three
//     render flags) (5 detours).
// Three groups because each can be tried and bisected alone: the previews
// only feed the drawable (visible, no city state), the rule match is pure
// memory work over the RUL rule files and the world cache, and the
// highlights call the demolition service (in query mode, and once for real
// in slot 60) and set flags on the city's occupants.
//
// Every function was written from the disassembly. Calls between them go to
// the game's addresses (through the address entries), so each detour is
// independent of the others and OPENSC4_NETTOOL_SKIP bisects them.
//
// What the state is (evidence in the functions below):
//   - the drawable's cell tree (+0x10) and textured tree (+0x1C), both
//     STLport multimaps keyed by the cell key (z << 16) + x: a cell node's
//     value (0x6C bytes) is the key and a 0x68-byte record of four vertices
//     (x, y, z, colour, u, v; 0x18 bytes each), four corner bytes and a word
//     (the type's preview height, below); a textured node adds a texture
//     binding (+0x7C, referenced through its +0xC count);
//   - the tool's +0x210 is the highlight region (an SC4CellRegion, 0x24
//     bytes: the rectangle, then the cells part with a bit a cell), +0x214 /
//     +0x218 the highlighted occupants (an SC4RefSet ordered by the
//     occupants' addresses: the set the demolition service collects in query
//     mode), +0x220 the demolition cost of the last query (eight bytes). The
//     life code's names for +0x210 to +0x218 (the "preview list" and the
//     "cell previews") are these; nettool_sim.h is left as it is.
//
// Left as the game's (the Renderer thread's: they compute what is drawn):
//   - 0x0061FE60 (ret 0x20): the four vertices of a cell's record: the
//     corners' positions in metres from the corner table 0x00AA82D0 picked by
//     the terrain's slot 0x118 and the step tables 0x00AA82AC/0x00AA82BC
//     (fild of cells * 16), the colour, the texture coordinates turned and
//     flipped, then the heights through 0x0061FBF0. Geometry, colours and
//     UVs: drawing. AddCell calls it at its address.
//   - 0x0061FBF0 (ret 0x10): the four vertex heights from the terrain (slot
//     0x14C, 0x38 and 0x84, x87) or the cell's own heights, plus the
//     record's height word. Drawing (and x87 with values from the terrain's
//     ST0); only 0x0061FE60 and the drawable's 0x00620930 call it.
//   - 0x00620BC0 (ret 0x20): a textured cell: the record (0x0061FE60), the
//     texture binding fetched from the texture service 0x00B43CD4 (slot
//     0x18, the TGI AddCellPreview built), the node inserted (0x00620A90,
//     ours) or, without a texture, AddCell (0x00620820, ours); the bindings
//     released (0x008090E0). It calls the texture service: drawing by the
//     rule of this part, so the game's; AddCellPreview calls it at its
//     address and it calls ours at theirs.
// The split: what is drawn (vertices, heights, UVs, colours of a vertex, the
// texture) stays the game's; which cells are previewed (the trees' nodes,
// their insert and erase), which occupants are highlighted (the region, the
// set, the highlight flags through the occupant's own SetHighlight) and the
// network occupant's flags are ours. AddCellPreview passes the game's
// drawing two values it picks itself: a colour (0xFF0000FF when the cell
// info's byte +0x52 is set, else 0xFF664D00) and the texture's TGI (type
// 0x7AB50E44, group 0x1ABE787D, the instance from the network manager's
// MapPreviewTexture); both are plain selections handed on as words.
//
// NAM. None of these is guarded. The guarded 0x0062CD50 calls the collector
// 0x0061F7C0 (cdecl, five arguments; the caller pops 0x18 with an earlier
// push): after the call it pops, tests AL and writes ECX and EDX before
// reading them (0x0062D19D / 0x0062D1A1, or the branch at 0x0062D1F5); so a
// plain cdecl detour with AL the answer keeps its convention. NAM's DLL
// reads the tool and cell infos by layout: these functions keep the region,
// the set, the cost and the drawable's trees in the game's memory with the
// game's layout, allocated by the game's allocators in the game's order
// (SC4::stl_allocate / SC4::Free for nodes, vectors and set heads, operator
// new / delete for the region and the filters), and build through the
// game's template helpers where the game calls them (the tree rebalance
// 0x0040D5E0, the range erasers 0x006202E0 / 0x006206A0, EqualRange
// 0x00474150, the point vector's InsertN 0x007C61A0, the rectangle copy
// 0x00637E20, the region's ConstructCells 0x004422A0, SC4RefSet::
// EraseSubtree 0x004676A0). No copy is kept across a call.
// Wrappers: no file of src/core/game_*.cpp or *fix*.cpp detours or tests
// return addresses in 0x0061F5D0 to 0x00633FA0 (game_quiet.cpp only names
// 0x006208D0 in a comment; game_bulldoze.cpp's DemolishRegion wrapper tests
// return addresses in the bulldoze tool 0x004B8830 to 0x004B9E20, which the
// game's and our callers here are not in); sim_timers.cpp has nothing here.
//
// Floats: none in our code. The preview height by type (0x00AA83F0: 15.5
// for types 2, 8 and 9, else 0) is a float the game moves as a dword
// (`mov ecx, [eax*4+0xAA83F0]; push ecx`); ours moves the same word.
//
// Registers. Every direct game caller was scanned (ECX, EDX and EAX after
// the call, both branches; the vtable slots' callers in the tool's range at
// 0x00625C09, 0x006279E6, 0x0062D380, 0x0063B9C1, 0x0063BC4B): none reads
// ECX or EDX before writing it. EAX is read after the collector (AL: 0x00609DE3,
// 0x00629C64, 0x0062D191), 0x00633FA0, 0x006220D0, 0x00622040 (AL), slot 59
// (all of EAX: 0x0063B9C7 compares it with +0x78), 0x006204B0 (the node),
// 0x00621550 and 0x00621580 (the end): ours answer the same. The others
// answer what the game's leave where it is defined (the inserts: the out
// iterator's address; the walker's constructor: this; slot 60: what slot 17
// leaves); AddCell's EAX is an address of the game's stack, read by none of
// its five callers, and ours is undefined. So all are plain detours.
//
// Stack leftovers the game stores (ours are 0):
//   - AddCellPreview's flip: a dword of its frame whose low byte is the
//     piece's flip bit (or 0x00633FA0's) and whose three upper bytes are
//     never written; it is pushed as 0x00620BC0's sixth argument, which hands
//     it to 0x0061FE60, which reads only its low byte (`cmp byte ptr
//     [esp+0x34], 0`). No other reader; verdict: harmless, ours 0.
//   - 0x00622040 hands the walker's slot 0 a dword whose low byte is the
//     tile's direction and whose upper bytes are those of the walker's
//     address (the argument slot it stores the byte into); the slot (ours or
//     the game's 0x00623F40) passes it to DirectionOffsets 0x00620D80, which
//     reads `movzx eax, byte ptr [esp+4]`. Ours builds the same dword from
//     our walker's address; only the byte is read.
//   - 0x00621D30's new memo record leaves bytes +1 to +3 unwritten; every
//     copy (in place, 0x00621A40, 0x00621550, 0x00621580) copies byte, word,
//     word, so they never reach the vector.
// Other stores into the game's stack: the collector keeps the lot manager in
// its first argument's slot, then the fill byte of the region (0) at that
// slot's low byte (ConstructCells reads only the byte through the pointer:
// 0x00441AA4); 0x00620CF0 stores the key into its argument slot; the inserts
// 0x006205C0 overwrite two argument slots with the node; 0x006220D0's
// answer goes into 0x00633FA0's first-argument slot. Never read again as
// what they were; ours use locals.
//
// Calls, each count from the asm's pushes for that call and the callee's own
// `ret N`:
//   - the part's own, at the game's addresses: AddCell five (ret 0x14),
//     EraseCell one (ret 4), the cell node insert five (ret 0x14), the
//     textured insert two (ret 8), its node insert five (ret 0x14), its node
//     maker one (ret 4), the piece lookup five (ret 0x14), the walker's
//     constructor five (ret 0x14), the rule file's match two (ret 8), the
//     entry's match two (ret 8), the cached edge lookup two (ret 8), the
//     collector cdecl five, the diff cdecl two, the check vector's
//     InsertOverflow five (ret 0x14), UninitializedFillN and
//     UninitializedCopy cdecl four;
//   - the drawing kept the game's: 0x0061FE60 eight (ret 0x20), 0x00620BC0
//     eight (ret 0x20);
//   - the tool's: slot 17 ClearCellPreviews (+0x44) none; the cell entry
//     0x00633220 one (ret 4); DirectionStep 0x00620DA0 four (ret 0x10);
//   - the walker's (vtable 0x00AA8700): slot 0 one (ret 4), slot 1 two (ret 8);
//   - SC4::GetNetworkManager 0x00602680 cdecl none; the network manager's
//     MapPreviewTexture (slot 13, +0x34) two (ret 8: the ID, the turn byte's
//     address);
//   - the network occupant (slot 80's argument): HasNetworkFlag +0x20,
//     SetNetworkFlag +0x28, ClearNetworkFlag +0x2C one each (ret 4);
//   - the occupants of the set: SetHighlight +0x44 two (ret 8);
//   - the city (0x00B43CEC) slot 0x84 none (the lot manager); the lot
//     manager slot 0x18 three (x, z, 0); a lot slot 0x38 one (the
//     rectangle out); the zone manager (0x00B43D14) slot 0x30 two (x, z);
//     the occupant manager (0x00B43D0C) slot 0x80 four (out, x, z, filter;
//     AL), the occupant's Release (slot 8); the demolition service
//     (0x00B43D00) slot 0x18 DemolishRegion eleven (ret 0x2C);
//   - the occupant filter: operator new 0x005E55E0 cdecl one (0xC), the
//     base constructor 0x005EA080 none, AddRef (+4) and Release (+8) none;
//   - template code: SC4::stl_allocate 0x0090CF54 and SC4::Free 0x0090CF63
//     cdecl one; operator delete 0x005E5620 cdecl one; SC4Tree::
//     RebalanceForInsert 0x0040D5E0 cdecl two (node, root*);
//     SC4Multimap::EqualRange 0x00474150 two (ret 8: out pair, key*); the
//     drawable's range erasers 0x006202E0 / 0x006206A0 two (ret 8); the
//     texture binding's AddRef 0x004664A0 none (code folded with the
//     demolition service's AddRef: `inc [ecx+0xC]`); SC4PointVector::InsertN
//     0x007C61A0 five (ret 0x14); SC4Vector16::UninitializedCopy 0x00637E20
//     cdecl four; SC4CellRegion::ConstructCells 0x004422A0 three (ret 0xC:
//     width, depth, the fill byte's address); SC4RefSet::EraseSubtree
//     0x004676A0 one (ret 4).
#include "core/nettool_sim.h"

#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace opensc4::nettool_sim {
namespace {

// The game's allocator and free (cdecl, one argument), operator new and
// delete.
char* Allocate(uint32_t size)
{
    return CallCdecl<char*, uint32_t>(addr::SC4_stl_allocate, size);
}
void Free(void* block)
{
    CallCdecl<void, void*>(addr::SC4_Free, block);
}
void FreeIfSet(void* block)
{
    if (block)
        Free(block);
}
char* New(uint32_t size)
{
    return CallCdecl<char*, uint32_t>(addr::SC4_operator_new, size);
}
void Delete(void* object)
{
    CallCdecl<void, void*>(addr::SC4_operator_delete, object);
}

// The node after `node` in order (the STL's increment, inlined by the game
// in every loop of this part).
char* Next(char* node)
{
    if (char* right = At<char*>(node, tree::kRight)) {
        while (char* left = At<char*>(right, tree::kLeft))
            right = left;
        return right;
    }
    char* parent = At<char*>(node, tree::kParent);
    while (node == At<char*>(parent, tree::kRight)) {
        node = parent;
        parent = At<char*>(parent, tree::kParent);
    }
    if (At<char*>(node, tree::kRight) != parent)
        node = parent;
    return node;
}

// The cell key the drawable's trees and the cell entry use.
uint32_t CellKey(const int32_t* xz)
{
    return (static_cast<uint32_t>(xz[1]) << 16) + static_cast<uint32_t>(xz[0]);
}

// A tree's parent for an equal-key insert (the game's inlined search): from
// the head down, left when the key is below the node's, else right; the
// child slot found is always null.
char* InsertParent(char* treeObject, uint32_t key)
{
    char* parent = At<char*>(treeObject, tree::kHead);
    char* node = At<char*>(parent, tree::kParent);
    while (node) {
        parent = node;
        node = key < At<uint32_t>(node, tree::kValue) ? At<char*>(node, tree::kLeft) : At<char*>(node, tree::kRight);
    }
    return parent;
}

// The layouts of this part (the drawable's node values, the region, the
// highlight fields of the tool).
namespace pv {
// A preview record (what 0x0061FE60 fills): four vertices of 0x18 bytes,
// four corner bytes +0x60, the type's height word +0x64.
inline constexpr std::size_t kRecordSize = 0x68;
// A node's value: the key, the record (0x6C bytes); a textured one also the
// binding (0x70 bytes).
inline constexpr std::size_t kValueKey = 0x0, kValueRecord = 0x4, kCellValueSize = 0x6C, kValueTexture = 0x6C;
// Where a node's value starts and where the binding is.
inline constexpr std::size_t kNodeKey = 0x10, kNodeTexture = 0x7C;
} // namespace pv

namespace hl {
inline constexpr std::size_t kRegion = 0x210, kSet = 0x214, kSetCount = 0x218, kCost = 0x220;
inline constexpr std::size_t kSetHeadSize = 0x14;
// The region (SC4CellRegion, 0x24 bytes from operator new): the rectangle
// (x1, z1, x2, z2), the cells part at +0x10 (vtable, width +0x14, depth
// +0x18, words per row +0x1C, the rows +0x20: a row per x, a bit per z).
inline constexpr std::size_t kRegionSize = 0x24, kCells = 0x10, kCellsVtable = 0x10, kWidth = 0x14;
inline constexpr std::size_t kRows = 0x20;
// The occupant filter the collector and slot 60 make (0xC bytes).
inline constexpr std::size_t kFilterSize = 0xC, kFilterPart = 0x4;
// DemolishRegion (the demolition service's slot 6) and its flags.
inline constexpr std::size_t kDemolishRegion = 0x18;
inline constexpr uint32_t kPrivilege = 1, kFlags = 0x2001;
// The zone types the collector refuses (10 to 15: bits 0xFC00 of the mask).
inline constexpr uint32_t kRefusedZones = 0xFC00;
// A step whose third word is 3 is skipped (UpdateOccupantHighlights).
inline constexpr uint32_t kSkippedStep = 3;
inline constexpr std::size_t kRuleCellRecordSize = 0x14;
// The two SetHighlight values: the collected ones and the ones let go.
inline constexpr uint32_t kHighlighted = 5, kNotHighlighted = 0;
inline constexpr std::size_t kSetHighlight = 0x44;
} // namespace hl

// The preview texture's TGI type and group, the two colours.
inline constexpr uint32_t kPreviewTextureType = 0x7AB50E44, kPreviewTextureGroup = 0x1ABE787D;
inline constexpr uint32_t kColourMarked = 0xFF0000FF, kColourPlain = 0xFF664D00;
// The networks AddCellPreview treats as layered (types 2, 8 and 9).
inline constexpr uint32_t kLayeredFlags = 0x304;
inline constexpr std::size_t kMapPreviewTexture = 0x34;

// ====================================================================================
// The drawable's trees (this the drawable, or its tree at +0x10 / +0x1C)
// ====================================================================================

// 0x006203E0 (ret 0x14: the iterator out, x, y, the value, a flag; this the
// cell tree +0x10): STLport's _M_insert: a node of 0x7C bytes with a copy of
// the 0x6C-byte value, put left of y when y is the head, x is set, or the
// flag is clear and the key is below y's (unsigned), else right; the head's
// leftmost/root/rightmost kept; linked, rebalanced (0x0040D5E0), the count
// up. EAX the iterator out.
char** __fastcall InsertCellNode(char* self, void*, char** out, char* x, char* y, const char* value, uint32_t flag)
{
    static bool logged = false;
    FirstCall("InsertCellNode", logged);
    char* const head = At<char*>(self, tree::kHead);
    const bool left = y == head || (flag == 0 && (x != nullptr || At<uint32_t>(const_cast<char*>(value), 0) <
                                                                       At<uint32_t>(y, tree::kValue)));
    char* const node = Allocate(preview::kCellNodeSize);
    if (reinterpret_cast<std::uintptr_t>(node) + pv::kNodeKey != 0)
        std::memcpy(node + pv::kNodeKey, value, pv::kCellValueSize);
    if (left) {
        At<char*>(y, tree::kLeft) = node;
        char* const h = At<char*>(self, tree::kHead);
        if (y == h) {
            At<char*>(h, tree::kParent) = node;
            At<char*>(At<char*>(self, tree::kHead), tree::kRight) = node;
        } else if (y == At<char*>(h, tree::kLeft)) {
            At<char*>(h, tree::kLeft) = node;
        }
    } else {
        At<char*>(y, tree::kRight) = node;
        char* const h = At<char*>(self, tree::kHead);
        if (y == At<char*>(h, tree::kRight))
            At<char*>(h, tree::kRight) = node;
    }
    At<char*>(node, tree::kParent) = y;
    At<char*>(node, tree::kLeft) = nullptr;
    At<char*>(node, tree::kRight) = nullptr;
    CallCdecl<void, char*, char**>(addr::SC4Tree_RebalanceForInsert, node,
                                   &At<char*>(At<char*>(self, tree::kHead), tree::kParent));
    ++At<uint32_t>(self, tree::kCount);
    *out = node;
    return out;
}

// 0x006204B0 (ret 4: the value; this not read): a textured node of 0x80
// bytes with a copy of the 0x70-byte value; the binding (+0x7C) referenced
// (0x004664A0) when set. EAX the node.
char* __fastcall CreateTexturedNode(char*, void*, const char* value)
{
    static bool logged = false;
    FirstCall("CreateTexturedNode", logged);
    char* const node = Allocate(preview::kTexturedNodeSize);
    if (reinterpret_cast<std::uintptr_t>(node) + pv::kNodeKey != 0) {
        std::memcpy(node + pv::kNodeKey, value, pv::kCellValueSize);
        char* const binding = At<char*>(const_cast<char*>(value), pv::kValueTexture);
        At<char*>(node, pv::kNodeTexture) = binding;
        if (binding)
            CallThis<void>(addr::SC4TextureBinding_AddRef, binding);
    }
    return node;
}

// 0x006205C0 (ret 0x14: as 0x006203E0; this the textured tree +0x1C): the
// same insert; a node put right is made by 0x006204B0 (at its address), one
// put left inline the same way (the copy, the binding referenced). EAX the
// iterator out.
char** __fastcall InsertTexturedNode(char* self, void*, char** out, char* x, char* y, const char* value,
                                     uint32_t flag)
{
    static bool logged = false;
    FirstCall("InsertTexturedNode", logged);
    const bool left = y == At<char*>(self, tree::kHead) ||
                      (flag == 0 && (x != nullptr || At<uint32_t>(const_cast<char*>(value), 0) <
                                                         At<uint32_t>(y, tree::kValue)));
    char* node = nullptr;
    if (!left) {
        node = CallThis<char*, const char*>(addr::cSC4NetworkToolPreview_CreateTexturedNode, self, value);
        At<char*>(y, tree::kRight) = node;
        char* const h = At<char*>(self, tree::kHead);
        if (y == At<char*>(h, tree::kRight))
            At<char*>(h, tree::kRight) = node;
    } else {
        node = Allocate(preview::kTexturedNodeSize);
        if (reinterpret_cast<std::uintptr_t>(node) + pv::kNodeKey != 0) {
            std::memcpy(node + pv::kNodeKey, value, pv::kCellValueSize);
            char* const binding = At<char*>(const_cast<char*>(value), pv::kValueTexture);
            At<char*>(node, pv::kNodeTexture) = binding;
            if (binding)
                CallThis<void>(addr::SC4TextureBinding_AddRef, binding);
        }
        At<char*>(y, tree::kLeft) = node;
        char* const h = At<char*>(self, tree::kHead);
        if (y == h) {
            At<char*>(h, tree::kParent) = node;
            At<char*>(At<char*>(self, tree::kHead), tree::kRight) = node;
        } else if (y == At<char*>(h, tree::kLeft)) {
            At<char*>(h, tree::kLeft) = node;
        }
    }
    At<char*>(node, tree::kParent) = y;
    At<char*>(node, tree::kLeft) = nullptr;
    At<char*>(node, tree::kRight) = nullptr;
    CallCdecl<void, char*, char**>(addr::SC4Tree_RebalanceForInsert, node,
                                   &At<char*>(At<char*>(self, tree::kHead), tree::kParent));
    ++At<uint32_t>(self, tree::kCount);
    *out = node;
    return out;
}

// 0x00620A90 (ret 8: the iterator out, the value; this the textured tree;
// the game's 0x00620BC0 calls it): an equal-key insert: the parent searched
// from the root (0x006205C0 with x null and the flag 0). EAX the iterator
// out.
char** __fastcall InsertTextured(char* self, void*, char** out, const char* value)
{
    static bool logged = false;
    FirstCall("InsertTextured", logged);
    char* const parent = InsertParent(self, At<uint32_t>(const_cast<char*>(value), 0));
    CallThis<char**, char**, char*, char*, const char*, uint32_t>(addr::cSC4NetworkToolPreview_InsertTexturedNode,
                                                                  self, out, nullptr, parent, value, 0u);
    return out;
}

// 0x00620820 (ret 0x14: the cell's x and z, a colour, the four heights, the
// type's height word, a flag; this the drawable; called by 0x00620BC0,
// 0x00624BF0, 0x006258C0, 0x006287D0 and 0x0064F2B0): the record made by the
// game's drawing (0x0061FE60 with no turn and no flip), then a cell node of
// the key and the record inserted into the cell tree (0x006203E0 with x null
// and the flag 0). The game's EAX is the address of its iterator local (what
// the insert answers); no caller reads it (all six sites of the five
// callers scanned), and an address of the game's stack cannot be given
// back, so ours answers nothing.
void __fastcall AddCell(char* self, void*, const int32_t* xz, uint32_t colour, const float* heights, uint32_t height,
                        uint32_t flag)
{
    static bool logged = false;
    FirstCall("AddCell", logged);
    alignas(4) char record[pv::kRecordSize] = {};
    CallThis<void, char*, const int32_t*, uint32_t, const float*, uint32_t, uint32_t, uint32_t, uint32_t>(
        addr::cSC4NetworkToolPreview_MakeVertices, self, record, xz, colour, heights, 0u, 0u, height, flag);
    alignas(4) char value[pv::kCellValueSize] = {};
    const uint32_t key = CellKey(xz);
    At<uint32_t>(value, pv::kValueKey) = key;
    std::memcpy(value + pv::kValueRecord, record, pv::kRecordSize);
    char* const cells = self + preview::kCells;
    char* const parent = InsertParent(cells, key);
    char* iterator = nullptr;
    CallThis<char**, char**, char*, char*, const char*, uint32_t>(addr::cSC4NetworkToolPreview_InsertCellNode, cells,
                                                                  &iterator, nullptr, parent, value, 0u);
}

// 0x00620CF0 (ret 4: the cell's x and z; this the drawable; called by
// AddCellPreview, 0x00624BF0 and 0x006287D0): every node of the cell taken
// out of the cell tree, then of the textured tree (EqualRange 0x00474150
// and the range erasers 0x006202E0 / 0x006206A0 at their addresses).
void __fastcall EraseCell(char* self, void*, const int32_t* xz)
{
    static bool logged = false;
    FirstCall("EraseCell", logged);
    struct Range {
        char* lower;
        char* upper;
    };
    uint32_t key = CellKey(xz);
    Range range{nullptr, nullptr};
    CallThis<void, Range*, const uint32_t*>(addr::SC4Multimap_EqualRange, self + preview::kCells, &range, &key);
    CallThis<void, char*, char*>(addr::cSC4NetworkToolPreview_EraseCellRange, self + preview::kCells, range.lower,
                                 range.upper);
    key = CellKey(xz);
    CallThis<void, Range*, const uint32_t*>(addr::SC4Multimap_EqualRange, self + preview::kTextured, &range, &key);
    CallThis<void, char*, char*>(addr::cSC4NetworkToolPreview_EraseTexturedRange, self + preview::kTextured,
                                 range.lower, range.upper);
}

// ====================================================================================
// Slot 58
// ====================================================================================

// 0x006246A0 (slot 58, ret 8: a cell info, its solved cell (+0x0 the piece
// ID, +0x4 the turn in bits 0-6 and the flip in bit 7); slot 55 0x006258C0
// calls it): nothing unless the cell is in the current placement (+0x10).
// The texture's instance from the network manager's MapPreviewTexture (the
// piece ID + the tool's +0xFC, the turn's address), the colour (+0x52), the
// cell's previews erased (0x00620CF0). Then, when the cell's flags have a
// layered network (0x304) and another one: for each of the 13 types whose
// flag the cell has, the piece the type's rule files give the cell
// (0x00633FA0); when there is one, its texture instance and a textured cell
// (the game's 0x00620BC0) with the type's preview height and 0. Else: the
// type of the lowest flag (none: nothing) and one textured cell with its
// height and the cell's byte +0x56.
void __fastcall AddCellPreview(char* self, void*, char* info, const char* piece)
{
    static bool logged = false;
    FirstCall("AddCellPreview", logged);
    if (!At<uint8_t>(info, cell::kInPlacement))
        return;
    uint32_t key[3] = {0, 0, 0};
    void* const manager = CallCdecl<void*>(addr::SC4_GetNetworkManager);
    const int32_t xz[2] = {At<int32_t>(info, cell::kX), At<int32_t>(info, cell::kZ)};
    const uint8_t bits = At<uint8_t>(const_cast<char*>(piece), 4);
    // The game's flip is a dword of its frame with only the low byte written
    // (see the top of the file); ours is 0 above it.
    uint32_t flip = static_cast<uint32_t>(bits >> 7);
    uint8_t turn = static_cast<uint8_t>(bits & 0x7F);
    key[0] = kPreviewTextureType;
    key[1] = kPreviewTextureGroup;
    key[2] = Virtual<uint32_t>(manager, kMapPreviewTexture,
                               At<uint32_t>(self, field::kRefreshWord) + At<uint32_t>(const_cast<char*>(piece), 0),
                               &turn);
    const uint32_t colour = At<uint8_t>(info, cell::kHasFlag100000) ? kColourMarked : kColourPlain;
    char* const drawable = At<char*>(self, field::kPreview);
    CallThis<void, const int32_t*>(addr::cSC4NetworkToolPreview_EraseCell, drawable, xz);
    const uint32_t flags = At<uint32_t>(info, cell::kFlags);
    using TexturedFn = void(__thiscall*)(char*, const int32_t*, uint32_t, char*, uint32_t*, uint32_t, uint32_t,
                                         uint32_t, uint32_t);
    const auto textured = reinterpret_cast<TexturedFn>(addr::cSC4NetworkToolPreview_AddTexturedCell);
    if ((flags & kLayeredFlags) && (flags & ~kLayeredFlags)) {
        uint32_t pieceId = 0;
        for (uint32_t type = 0; type < kTypes; ++type) {
            if (!(At<uint32_t>(info, cell::kFlags) & Global<uint32_t>(addr::cSC4NetworkTool_TypeFlags + type * 4)))
                continue;
            const uint8_t found = CallThis<uint8_t, const int32_t*, uint32_t, uint32_t*, uint8_t*, uint8_t*>(
                addr::cSC4NetworkTool_FindPreviewPiece, self, xz, type, &pieceId, &turn,
                reinterpret_cast<uint8_t*>(&flip));
            if (!found)
                continue;
            key[2] = Virtual<uint32_t>(manager, kMapPreviewTexture, At<uint32_t>(self, field::kRefreshWord) + pieceId,
                                       &turn);
            textured(At<char*>(self, field::kPreview), xz, colour, info + cell::kHeights, key, turn, flip,
                     Global<uint32_t>(addr::cSC4NetworkTool_PreviewHeightByType + type * 4), 0u);
        }
        return;
    }
    const uint32_t lowest = flags & ~(flags - 1);
    uint32_t type = 0;
    while (lowest != Global<uint32_t>(addr::cSC4NetworkTool_TypeFlags + type * 4)) {
        if (++type >= kTypes)
            return;
    }
    textured(At<char*>(self, field::kPreview), xz, colour, info + cell::kHeights, key, turn, flip,
             Global<uint32_t>(addr::cSC4NetworkTool_PreviewHeightByType + type * 4),
             static_cast<uint32_t>(At<uint8_t>(info, cell::kNetworkLot)));
}

// ====================================================================================
// The piece of a cell from the rule files
// ====================================================================================

// 0x00627080 (ret 0x14: x, z, the tool, a type, a byte; this the walker,
// 0x28 bytes; called by 0x00633FA0 and 0x006340D0): the walker's vtable
// 0x00AA8700, its memo vector +0x8 empty, the cell, the tool, the type, the
// mask (0 when the byte is set, else every network but the type's), +0x4
// the cell's edge word for the type when the tool's cell entry 0x00633220
// has one, else 0. EAX this.
char* __fastcall NeighborConstruct(char* self, void*, int32_t x, int32_t z, char* tool, uint32_t type, uint32_t all)
{
    static bool logged = false;
    FirstCall("NeighborConstruct", logged);
    At<uint32_t>(self, 0x4) = 0;
    At<uint32_t>(self, 0x8) = 0;
    At<uint32_t>(self, 0xC) = 0;
    At<uint32_t>(self, 0x10) = 0;
    At<char*>(self, walker::kTool) = tool;
    At<std::uintptr_t>(self, 0) = addr::cSC4NetworkTool_NeighborVtable;
    At<int32_t>(self, walker::kX) = x;
    At<int32_t>(self, walker::kZ) = z;
    At<uint32_t>(self, walker::kType) = type;
    At<uint32_t>(self, walker::kMask) =
        static_cast<uint8_t>(all) ? 0u : (Global<uint32_t>(addr::cSC4NetworkTool_TypeFlags + type * 4) ^ 0x1FFFu);
    At<uint32_t>(self, 0x4) = 0;
    const uint32_t key = (static_cast<uint32_t>(z) << 16) + static_cast<uint32_t>(x);
    char* const entry =
        CallThis<char*, uint32_t>(addr::cSC4NetworkManager_ToolCellEntry, At<char*>(self, walker::kTool), key);
    if (entry)
        At<uint32_t>(self, 0x4) = At<uint32_t>(entry, cell::kByType + 4 * At<uint32_t>(self, walker::kType));
    return self;
}

// The walker's memo (+0x8): a vector of 12-byte records (a direction byte,
// the neighbour's edge word, a word 0).
namespace memo {
inline constexpr std::size_t kVector = 0x8, kSize = 0xC, kEdges = 0x4, kWord8 = 0x8;
inline constexpr std::size_t kEdge = 0x4; // the walker's own edge word
} // namespace memo

// A 12-byte check or memo record copied as the game copies it: the byte,
// then the two words.
void CopyRecord(char* to, const char* from)
{
    At<uint8_t>(to, 0) = At<uint8_t>(const_cast<char*>(from), 0);
    At<uint32_t>(to, 4) = At<uint32_t>(const_cast<char*>(from), 4);
    At<uint32_t>(to, 8) = At<uint32_t>(const_cast<char*>(from), 8);
}

// 0x00621D30 (ret 8: a direction, the edge word out; this the walker;
// 0x00622040's): the memo's record of the direction's byte when there is
// one; else the walker's slot 1 (the direction as given, the record's word
// out), the record appended (in place, or the check vector's InsertOverflow
// 0x00621A40 when full) and its word answered.
void __fastcall NeighborEdgesCached(char* self, void*, uint32_t direction, uint32_t* out)
{
    static bool logged = false;
    FirstCall("NeighborEdgesCached", logged);
    Vector& v = At<Vector>(self, memo::kVector);
    for (char* at = v.begin; at != v.end; at += memo::kSize) {
        if (At<uint8_t>(at, 0) == static_cast<uint8_t>(direction)) {
            *out = At<uint32_t>(at, memo::kEdges);
            return;
        }
    }
    alignas(4) char record[memo::kSize] = {};
    At<uint32_t>(record, memo::kEdges) = 0;
    At<uint32_t>(record, memo::kWord8) = 0;
    At<uint8_t>(record, 0) = static_cast<uint8_t>(direction);
    Virtual<char*>(self, walker::kEdgesSlot, direction, &At<uint32_t>(record, memo::kEdges));
    if (v.end == v.capacity) {
        uint8_t tag = 0;
        CallThis<void, char*, const char*, void*, uint32_t, uint32_t>(addr::SC4RuleCheckVector_InsertOverflow, &v,
                                                                      v.end, record, &tag, 1u, 1u);
        *out = At<uint32_t>(record, memo::kEdges);
        return;
    }
    if (v.end)
        CopyRecord(v.end, record);
    v.end += memo::kSize;
    *out = At<uint32_t>(record, memo::kEdges);
}

// 0x00622040 (ret 8: the walker, the tiles' vector out; this an entry of a
// rule file, 0x20 bytes; 0x006220D0's): each check (12 bytes) must see its
// edges under its mask at the neighbour of its position (0x00621D30; the
// word is 0 before the first and kept between them); each tile's position
// must be clear (the walker's slot 0). Then the out gets the entry's tile
// vector (+0x10); AL 1. Else AL 0, the out left as it was.
uint8_t __fastcall EntryMatches(char* self, void*, char* walkerObject, char** out)
{
    static bool logged = false;
    FirstCall("EntryMatches", logged);
    uint32_t edges = 0;
    const Vector& checks = At<Vector>(self, 0x4);
    for (char* at = checks.begin; at != checks.end; at += memo::kSize) {
        CallThis<void, uint32_t, uint32_t*>(addr::cSC4NetworkTool_NeighborEdgesCached, walkerObject,
                                            static_cast<uint32_t>(At<uint8_t>(at, 0)), &edges);
        if ((At<uint32_t>(at, 4) ^ edges) & At<uint32_t>(at, 8))
            return 0;
    }
    const Vector& tiles = At<Vector>(self, 0x10);
    for (char* at = tiles.begin; at != tiles.end; at += 8) {
        // The game stores the position byte into the low byte of its
        // argument slot that holds the walker and pushes the whole word.
        const uint32_t direction = static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(walkerObject) & ~0xFFu) |
                                   At<uint8_t>(at, 4);
        if (!static_cast<uint8_t>(Virtual<uint32_t>(walkerObject, walker::kIsClearSlot, direction)))
            return 0;
    }
    *out = self + 0x10;
    return 1;
}

// 0x006220D0 (ret 8: the walker, the tiles' vector out; this a rule file:
// its multimap at +0x4; called by 0x00633FA0 and 0x006340D0): the out
// cleared; the entries whose key is the walker's own edge word (+0x4; the
// lower and upper bound searched from the root), in order, until one matches
// (0x00622040): AL 1; else AL 0.
uint8_t __fastcall RuleFileMatch(char* self, void*, char* walkerObject, char** out)
{
    static bool logged = false;
    FirstCall("RuleFileMatch", logged);
    *out = nullptr;
    char* const head = At<char*>(self, 0x4);
    const uint32_t key = At<uint32_t>(walkerObject, memo::kEdge);
    char* lower = head;
    for (char* node = At<char*>(head, tree::kParent); node;) {
        if (At<uint32_t>(node, tree::kValue) >= key) {
            lower = node;
            node = At<char*>(node, tree::kLeft);
        } else {
            node = At<char*>(node, tree::kRight);
        }
    }
    char* upper = At<char*>(self, 0x4);
    const uint32_t again = At<uint32_t>(walkerObject, memo::kEdge);
    for (char* node = At<char*>(upper, tree::kParent); node;) {
        if (again < At<uint32_t>(node, tree::kValue)) {
            upper = node;
            node = At<char*>(node, tree::kLeft);
        } else {
            node = At<char*>(node, tree::kRight);
        }
    }
    for (char* node = lower; node != upper; node = Next(node)) {
        if (CallThis<uint8_t, char*, char**>(addr::SC4RuleFile_EntryMatches, node + 0x14, walkerObject, out))
            return 1;
    }
    return 0;
}

// 0x00633FA0 (ret 0x14: the cell's x and z, a type, the piece out, the turn
// byte out, the flip byte out; this the tool; called by AddCellPreview and
// slot 55 0x006258C0): for each rule file of the type's row (+0x24; a null
// one ends the search), a walker of the cell (0x00627080 with the byte 1)
// and the file's match (0x006220D0); in the matching entry's tiles the one
// whose position steps (DirectionStep 0x00620DA0) onto the cell itself gives
// the piece, the turn (+0x5 & 0x7F) and the flip (+0x5 >> 7): AL 1. The
// walker's memo is freed after each file. AL 0 when none gives one.
uint8_t __fastcall FindPreviewPiece(char* self, void*, const int32_t* xz, uint32_t type, uint32_t* pieceOut,
                                    uint8_t* turnOut, uint8_t* flipOut)
{
    static bool logged = false;
    FirstCall("FindPreviewPiece", logged);
    char* const typeRow = reinterpret_cast<char*>(addr::cSC4NetworkConstructionCrew_NetworkToolTuning + type * kRowSize);
    const Vector& files = At<Vector>(typeRow, 0x24);
    const int32_t count = static_cast<int32_t>(files.end - files.begin) >> 2;
    for (int32_t i = 0; i < count; ++i) {
        char* const file = reinterpret_cast<char**>(At<Vector>(typeRow, 0x24).begin)[i];
        if (!file)
            return 0;
        alignas(4) char walkerObject[walker::kSize] = {};
        CallThis<char*, int32_t, int32_t, char*, uint32_t, uint32_t>(addr::cSC4NetworkTool_NeighborConstruct,
                                                                     walkerObject, xz[0], xz[1], self, type, 1u);
        char* tiles = nullptr;
        if (CallThis<uint8_t, char*, char**>(addr::SC4RuleFile_Match, file, walkerObject, &tiles)) {
            char* const end = At<char*>(tiles, 4);
            for (char* tile = At<char*>(tiles, 0); tile != end; tile += 8) {
                int32_t x = 0, z = 0;
                CallThis<int32_t, int32_t, int32_t, int32_t*, int32_t*>(addr::cSC4NetworkTool_DirectionStep, tile,
                                                                        xz[0], xz[1], &x, &z);
                if (xz[0] == x && xz[1] == z) {
                    *pieceOut = At<uint32_t>(tile, 0);
                    *turnOut = static_cast<uint8_t>(At<uint8_t>(tile, 5) & 0x7F);
                    *flipOut = static_cast<uint8_t>(At<uint8_t>(tile, 5) >> 7);
                    FreeIfSet(At<Vector>(walkerObject, memo::kVector).begin);
                    return 1;
                }
            }
        }
        FreeIfSet(At<Vector>(walkerObject, memo::kVector).begin);
    }
    return 0;
}

// 0x00621550 (cdecl: where, a count, the value, an unread tag; 0x00621A40's):
// the count copies of a 12-byte record (byte, word, word; nothing written
// where `where` is null); EAX the end.
char* __cdecl CheckUninitializedFillN(char* where, uint32_t count, const char* value, void*)
{
    static bool logged = false;
    FirstCall("CheckUninitializedFillN", logged);
    for (; count; --count) {
        if (where)
            CopyRecord(where, value);
        where += memo::kSize;
    }
    return where;
}

// 0x00621580 (cdecl: first, last, where, an unread tag; called by
// 0x00621850 and 0x00621A40): the range copied (byte, word, word; nothing
// written where `where` is null); EAX the end.
char* __cdecl CheckUninitializedCopy(const char* first, const char* last, char* where, void*)
{
    static bool logged = false;
    FirstCall("CheckUninitializedCopy", logged);
    for (; first != last; first += memo::kSize, where += memo::kSize)
        if (where)
            CopyRecord(where, first);
    return where;
}

// 0x00621A40 (ret 0x14: where, the value, an unread tag, a count, at the end;
// this a vector of 12-byte records; called by 0x00621D30, 0x00621DF0,
// 0x00621F30 and 0x00622870): the vector grown to its size + max(size,
// count) (SC4::stl_allocate; none for 0), the elements before `where`
// copied, the count copies of the value (one inline, more through
// 0x00621550), the elements after `where` (0x00621580) unless the flag's
// byte is set, the old memory freed.
void __fastcall CheckInsertOverflow(Vector* self, void*, char* where, const char* value, void*, uint32_t count,
                                    uint32_t atEnd)
{
    static bool logged = false;
    FirstCall("CheckInsertOverflow", logged);
    const uint32_t size =
        static_cast<uint32_t>(static_cast<int32_t>(self->end - self->begin) / static_cast<int32_t>(memo::kSize));
    const uint32_t grown = (size < count ? count : size) + size;
    char* const buffer = grown ? Allocate(grown * static_cast<uint32_t>(memo::kSize)) : nullptr;
    char* at = buffer;
    for (char* from = self->begin; from != where; from += memo::kSize, at += memo::kSize)
        if (at)
            CopyRecord(at, from);
    if (count == 1) {
        if (at)
            CopyRecord(at, value);
        at += memo::kSize;
    } else {
        at = CallCdecl<char*, char*, uint32_t, const char*, uint32_t*>(addr::SC4RuleCheckVector_UninitializedFillN, at,
                                                                       count, value, &atEnd);
    }
    if (!static_cast<uint8_t>(atEnd))
        at = CallCdecl<char*, char*, char*, char*, uint32_t*>(addr::SC4RuleCheckVector_UninitializedCopy, where,
                                                              self->end, at, &atEnd);
    FreeIfSet(self->begin);
    self->end = at;
    self->begin = buffer;
    self->capacity = buffer + grown * static_cast<uint32_t>(memo::kSize);
}

// ====================================================================================
// Highlights
// ====================================================================================

// 0x0061F5D0 (cdecl: the old set, the new set; UpdateOccupantHighlights'):
// the two SC4RefSets (ordered by the occupants' addresses) walked together:
// an occupant only in the old one gets SetHighlight(0, 0), one only in the
// new one SetHighlight(5, 0), one in both nothing.
void __cdecl DiffHighlights(char* oldSet, char* newSet)
{
    static bool logged = false;
    FirstCall("DiffHighlights", logged);
    char* const oldHead = At<char*>(oldSet, tree::kHead);
    char* const newHead = At<char*>(newSet, tree::kHead);
    char* left = At<char*>(oldHead, tree::kLeft);
    char* right = At<char*>(newHead, tree::kLeft);
    while (left != oldHead) {
        if (right == newHead) {
            for (; left != oldHead; left = Next(left))
                Virtual<uint8_t>(At<void*>(left, tree::kValue), hl::kSetHighlight, hl::kNotHighlighted, 0u);
            break;
        }
        const std::uintptr_t was = At<std::uintptr_t>(left, tree::kValue);
        const std::uintptr_t now = At<std::uintptr_t>(right, tree::kValue);
        if (now > was) {
            Virtual<uint8_t>(At<void*>(left, tree::kValue), hl::kSetHighlight, hl::kNotHighlighted, 0u);
            left = Next(left);
        } else if (now == was) {
            left = Next(left);
            right = Next(right);
        } else {
            Virtual<uint8_t>(At<void*>(right, tree::kValue), hl::kSetHighlight, hl::kHighlighted, 0u);
            right = Next(right);
        }
    }
    for (; right != newHead; right = Next(right))
        Virtual<uint8_t>(At<void*>(right, tree::kValue), hl::kSetHighlight, hl::kHighlighted, 0u);
}

// A new occupant filter of the highlights (0xC bytes, operator new; the base
// constructor 0x005EA080 at its address, then the two vtables); null when
// operator new gives none.
char* NewHighlightFilter()
{
    char* const filter = New(static_cast<uint32_t>(hl::kFilterSize));
    if (!filter)
        return nullptr;
    CallThis<void*>(addr::SC4_OccupantFilterBase, filter);
    At<std::uintptr_t>(filter, 0) = addr::cSC4NetworkTool_HighlightFilterVtable;
    At<std::uintptr_t>(filter, hl::kFilterPart) = addr::cSC4NetworkTool_HighlightFilterVtable2;
    return filter;
}

// A 16-byte rectangle appended to the collector's vector as the game's
// inlined push_back does: in place (nothing written at a null end), else the
// vector grown to size + max(size, 1) (SC4::stl_allocate), the old elements
// copied by 0x00637E20 (at its address; the tag unread), the rectangle
// written where the copy ended (when not null), the old memory freed.
void PushRect(Vector& v, const int32_t* rect)
{
    if (v.end != v.capacity) {
        if (v.end)
            std::memcpy(v.end, rect, 16);
        v.end += 16;
        return;
    }
    const uint32_t size = static_cast<uint32_t>(static_cast<int32_t>(v.end - v.begin) >> 4);
    const uint32_t grown = (size < 1 ? 1u : size) + size;
    char* const buffer = grown ? Allocate(grown << 4) : nullptr;
    uint32_t tag = 0;
    char* at = CallCdecl<char*, char*, char*, char*, uint32_t*>(addr::SC4Vector16_UninitializedCopy, v.begin, v.end,
                                                                buffer, &tag);
    if (at)
        std::memcpy(at, rect, 16);
    at += 16;
    FreeIfSet(v.begin);
    v.begin = buffer;
    v.capacity = buffer + (grown << 4);
    v.end = at;
}

// 0x0061F7C0 (cdecl: the cells (a vector of 8-byte points), the region out,
// the cost out (8 bytes), the set, the filter; called by
// UpdateOccupantHighlights, 0x00609A00 and the guarded 0x0062CD50). AL 1 at
// once (nothing written) for no cells. The bounding box of the cells and of
// the lots on them (the city's lot manager, slot 0x84: GetLot(x, z, 0), the
// lot's GetBoundingRect, its far edges + 1, the lots' rectangles kept); AL 1
// (nothing written) when it is empty in x. Else a region of the box (0x24
// bytes from operator new: the rectangle, cells of width x2 - x1 + 1 and
// depth z2 - z1 + 1 cleared, ConstructCells 0x004422A0) into the out; each
// cell of the lots' rectangles that has a zone (the zone manager's slot
// 0x30) and no occupant passing the filter (the occupant manager's slot
// 0x80, the occupant released) is marked and its zone type's bit gathered.
// AL 0 when a zone type 10 to 15 is among them. Else the cost cleared and
// DemolishRegion(0: only price and collect, the region, 1, 0x2001, 0, a new
// highlight filter (referenced, released after), the cost, the set, 0, 0,
// 0): its AL.
uint8_t __cdecl CollectHighlights(const Vector* cells, char** regionOut, uint32_t* cost, char* set, void* filter)
{
    static bool logged = false;
    FirstCall("CollectHighlights", logged);
    int32_t minX = 0x7FFFFFFF, minZ = 0x7FFFFFFF;
    Vector rects{nullptr, nullptr, nullptr};
    int32_t maxX = 0, maxZ = 0;
    const char* at = cells->begin;
    const char* const end = cells->end;
    void* const lots = Virtual<void*>(Global<void*>(addr::SC4_City), 0x84);
    if (at == end)
        return 1;
    for (; at != end; at += kPointSize) {
        const int32_t x = At<int32_t>(const_cast<char*>(at), 0), z = At<int32_t>(const_cast<char*>(at), 4);
        if (x < minX)
            minX = x;
        if (x >= maxX)
            maxX = x + 1;
        if (z < minZ)
            minZ = z;
        if (z >= maxZ)
            maxZ = z + 1;
        void* const lot = Virtual<void*>(lots, 0x18, x, z, 0u);
        if (!lot)
            continue;
        int32_t rect[4] = {0, 0, 0, 0};
        Virtual<void>(lot, 0x38, &rect[0]);
        rect[3] += 1;
        rect[2] += 1;
        if (minX >= rect[0])
            minX = rect[0];
        if (minZ >= rect[1])
            minZ = rect[1];
        if (maxX <= rect[2])
            maxX = rect[2];
        if (maxZ <= rect[3])
            maxZ = rect[3];
        PushRect(rects, rect);
    }
    if (maxX < minX) {
        FreeIfSet(rects.begin);
        return 1;
    }
    char* region = New(static_cast<uint32_t>(hl::kRegionSize));
    if (region) {
        At<int32_t>(region, 0x0) = minX;
        At<int32_t>(region, 0x4) = minZ;
        At<int32_t>(region, 0x8) = maxX;
        At<int32_t>(region, 0xC) = maxZ;
        uint8_t fill = 0;
        CallThis<void*, uint32_t, uint32_t, uint8_t*>(addr::SC4CellRegion_ConstructCells, region + hl::kCells,
                                                      static_cast<uint32_t>(maxX - minX + 1),
                                                      static_cast<uint32_t>(maxZ - minZ + 1), &fill);
    }
    *regionOut = region;
    void* const occupants = Global<void*>(addr::SC4_NetworkService);
    void* const zones = Global<void*>(addr::SC4_ZoneManager);
    uint32_t zoneBits = 0;
    for (char* r = rects.begin; r != rects.end; r += 16) {
        for (int32_t x = At<int32_t>(r, 0); x < At<int32_t>(r, 8); ++x) {
            for (int32_t z = At<int32_t>(r, 4); z < At<int32_t>(r, 12); ++z) {
                const uint32_t zone = Virtual<uint32_t>(zones, 0x30, x, z);
                if (!zone)
                    continue;
                void* occupant = nullptr;
                if (!Virtual<uint8_t>(occupants, 0x80, &occupant, x, z, filter)) {
                    char* const marked = *regionOut;
                    const uint32_t column = static_cast<uint32_t>(z - At<int32_t>(marked, 0x4));
                    uint32_t* const row = At<uint32_t**>(marked, hl::kRows)[x - At<int32_t>(marked, 0x0)];
                    row[column >> 5] |= 1u << (column & 31);
                    zoneBits |= 1u << (zone & 31);
                }
                if (occupant)
                    ReleaseOf(occupant);
            }
        }
    }
    if (zoneBits & hl::kRefusedZones) {
        FreeIfSet(rects.begin);
        return 0;
    }
    cost[0] = 0;
    cost[1] = 0;
    void* const demolition = Global<void*>(addr::SC4_Service43D00);
    char* const highlightFilter = NewHighlightFilter();
    if (highlightFilter)
        AddRefOf(highlightFilter);
    const uint8_t answer = Virtual<uint8_t>(demolition, hl::kDemolishRegion, 0u, *regionOut, hl::kPrivilege,
                                            hl::kFlags, 0u, static_cast<void*>(highlightFilter), cost, set, 0u, 0u, 0u);
    if (highlightFilter)
        ReleaseOf(highlightFilter);
    FreeIfSet(rects.begin);
    return answer;
}

// A point appended to UpdateOccupantHighlights' vector the first way the
// game inlines it: in place (nothing written at a null end), else the point
// vector's InsertN 0x007C61A0 (where, the point, an unread tag, 1, 1).
void PushPointInsertN(Vector& v, const int32_t* point)
{
    if (v.end != v.capacity) {
        if (v.end) {
            At<int32_t>(v.end, 0) = point[0];
            At<int32_t>(v.end, 4) = point[1];
        }
        v.end += kPointSize;
        return;
    }
    uint32_t tag = 0;
    CallThis<void, char*, const int32_t*, uint32_t*, uint32_t, uint32_t>(addr::SC4PointVector_InsertN, &v, v.end,
                                                                         point, &tag, 1u, 1u);
}

// The second way (the rule cell records' loop): in place, else the vector
// grown inline to size + max(size, 1) (SC4::stl_allocate), the points copied
// (none written at a null place), the new one, the old memory freed.
void PushPointInline(Vector& v, const int32_t* point)
{
    if (v.end != v.capacity) {
        if (v.end) {
            At<int32_t>(v.end, 0) = point[0];
            At<int32_t>(v.end, 4) = point[1];
        }
        v.end += kPointSize;
        return;
    }
    const uint32_t size = static_cast<uint32_t>(static_cast<int32_t>(v.end - v.begin) >> 3);
    const uint32_t grown = (size < 1 ? 1u : size) + size;
    char* const buffer = grown ? Allocate(grown * 8) : nullptr;
    char* at = buffer;
    for (char* from = v.begin; from != v.end; from += kPointSize, at += kPointSize) {
        if (at) {
            At<int32_t>(at, 0) = At<int32_t>(from, 0);
            At<int32_t>(at, 4) = At<int32_t>(from, 4);
        }
    }
    if (at) {
        At<int32_t>(at, 0) = point[0];
        At<int32_t>(at, 4) = point[1];
    }
    at += kPointSize;
    FreeIfSet(v.begin);
    v.end = at;
    v.begin = buffer;
    v.capacity = buffer + grown * 8;
}

// True when the tool's cell entry 0x00633220 for the point has no network
// lot cell (no entry, or its byte +0x56 clear): the cell is collected.
bool Collected(char* self, const int32_t* point)
{
    char* const entry = CallThis<char*, uint32_t>(addr::cSC4NetworkManager_ToolCellEntry, self, CellKey(point));
    return !entry || !At<uint8_t>(entry, cell::kNetworkLot);
}

// 0x006299A0 (slot 59, ret 4: a count; called by 0x0063B830, which the
// network placement and PlaceAllSegments call): with no valid step (+0x78),
// slot 17 and EAX 0. Else the cells of the valid steps (each step's cells
// from its first to its last index, steps of kind 3 skipped) and of the rule
// cell records (+0xF0) that are not network lot cells; the old region
// (+0x210) deleted and cleared; a new set; the collector (the cells, +0x210,
// +0x220, the new set, the filter +0x364); the diff of the old set (+0x214)
// and the new one (0x0061F5D0); the new set put in place and the old one
// emptied (SC4RefSet::EraseSubtree) and freed; the cells' memory freed. EAX
// the count, one less when the collector answered 0.
int32_t __fastcall UpdateOccupantHighlights(char* self, void*, int32_t count)
{
    static bool logged = false;
    FirstCall("UpdateOccupantHighlights", logged);
    if (At<int32_t>(self, field::kValidSteps) == 0) {
        Virtual<void>(self, slot::kClearCellPreviews);
        return 0;
    }
    Vector cells{nullptr, nullptr, nullptr};
    for (int32_t i = 0; i < At<int32_t>(self, field::kValidSteps); ++i) {
        const std::size_t offset = static_cast<std::size_t>(i) * step::kSize;
        char* const stepAt = At<char*>(self, field::kSteps) + offset;
        if (At<uint32_t>(stepAt, step::kIrregular) == hl::kSkippedStep)
            continue;
        for (uint32_t index = At<uint32_t>(stepAt, 0); index <= At<uint32_t>(At<char*>(self, field::kSteps) + offset, 4);
             ++index) {
            char* const pointAt = At<char*>(self, field::kCells) + index * kPointSize;
            const int32_t point[2] = {At<int32_t>(pointAt, 0), At<int32_t>(pointAt, 4)};
            if (Collected(self, point))
                PushPointInsertN(cells, point);
        }
    }
    {
        char* const last = At<Vector>(self, life::kRuleCells).end;
        for (char* record = At<Vector>(self, life::kRuleCells).begin; record != last;
             record += hl::kRuleCellRecordSize) {
            const int32_t point[2] = {At<int32_t>(record, 0), At<int32_t>(record, 4)};
            if (Collected(self, point))
                PushPointInline(cells, point);
        }
    }
    if (char* const region = At<char*>(self, hl::kRegion)) {
        At<std::uintptr_t>(region, hl::kCellsVtable) = addr::SC4CellRegion_CellsVtable;
        if (At<void**>(region, hl::kRows)) {
            for (uint32_t i = 0; i < At<uint32_t>(region, hl::kWidth); ++i)
                Delete(At<void**>(region, hl::kRows)[i]);
            Delete(At<void*>(region, hl::kRows));
        }
        Delete(region);
    }
    At<char*>(self, hl::kRegion) = nullptr;
    struct Set {
        char* head;
        uint32_t count;
    } fresh{nullptr, 0};
    fresh.head = Allocate(static_cast<uint32_t>(hl::kSetHeadSize));
    fresh.count = 0;
    At<uint8_t>(fresh.head, 0) = 0;
    At<char*>(fresh.head, tree::kParent) = nullptr;
    At<char*>(fresh.head, tree::kLeft) = fresh.head;
    At<char*>(fresh.head, tree::kRight) = fresh.head;
    const uint8_t collected = CallCdecl<uint8_t, Vector*, char*, char*, Set*, void*>(
        addr::cSC4NetworkTool_CollectHighlights, &cells, self + hl::kRegion, self + hl::kCost, &fresh,
        At<void*>(self, field::kFilter));
    CallCdecl<void, char*, Set*>(addr::cSC4NetworkTool_DiffHighlights, self + hl::kSet, &fresh);
    Set old{At<char*>(self, hl::kSet), At<uint32_t>(self, hl::kSetCount)};
    At<char*>(self, hl::kSet) = fresh.head;
    At<uint32_t>(self, hl::kSetCount) = fresh.count;
    int32_t answer = count;
    if (!collected)
        --answer;
    if (old.count) {
        CallThis<void, char*>(addr::SC4RefSet_EraseSubtree, &old, At<char*>(old.head, tree::kParent));
        At<char*>(old.head, tree::kLeft) = old.head;
        At<char*>(old.head, tree::kParent) = nullptr;
        At<char*>(old.head, tree::kRight) = old.head;
        old.count = 0;
    }
    FreeIfSet(old.head);
    FreeIfSet(cells.begin);
    return answer;
}

// 0x00624920 (slot 60; called by 0x0062CD50 and 0x0063B830): when there is
// a region (+0x210), DemolishRegion(1: remove, the region, 1, 0x2001, 0, a
// new highlight filter (not referenced and never released, as the game's),
// 0, 0, 0, 0, 0); then slot 17 (the game's tail jump). EAX what slot 17
// leaves.
uint32_t __fastcall DestroyHighlightedOccupants(char* self, void*)
{
    static bool logged = false;
    FirstCall("DestroyHighlightedOccupants", logged);
    if (At<char*>(self, hl::kRegion)) {
        void* const demolition = Global<void*>(addr::SC4_Service43D00);
        char* const filter = NewHighlightFilter();
        Virtual<uint8_t>(demolition, hl::kDemolishRegion, 1u, At<char*>(self, hl::kRegion), hl::kPrivilege,
                         hl::kFlags, 0u, static_cast<void*>(filter), 0u, 0u, 0u, 0u, 0u);
    }
    return Virtual<uint32_t>(self, slot::kClearCellPreviews);
}

// 0x00623CE0 (slot 80, ret 8: a network occupant, the address of its edge
// word for the type; slot 72 0x006278A0 calls it): the occupant's three
// render flags from the edge word. Without the flag 0x40000000 both
// triangle flags 0x2000000 and 0x4000000 are set. Else by the word:
//   9, 0x301, 0x309, 0xB00, 0xB01: 0x1000000 set, then 0x2000000 set and
//     0x4000000 cleared;
//   0xB, 0x100000B, 0x1000003, 0x9000000, 0x9000003: 0x2000000 set,
//     0x4000000 cleared;
//   0x900, 0x30100, 0x30900, 0xB0000, 0xB0100: 0x4000000 set, 0x2000000
//     cleared;
//   0x90000, 0x3010000, 0x3090000, 0xB000000, 0xB010000: 0x1000000 and
//     0x4000000 set, 0x2000000 cleared;
//   any other: 0x2000000 and 0x4000000 set.
// (The game's compare tree, flattened; the calls and their order are its.)
void __fastcall SetQuadTrianglesToDraw(char*, void*, void* occupant, const uint32_t* edges)
{
    static bool logged = false;
    FirstCall("SetQuadTrianglesToDraw", logged);
    constexpr std::size_t kHas = 0x20, kSet = 0x28, kClear = 0x2C;
    constexpr uint32_t kSplit = 0x40000000, kCorner = 0x1000000, kFirst = 0x2000000, kSecond = 0x4000000;
    if (!Virtual<uint8_t>(occupant, kHas, kSplit)) {
        Virtual<void>(occupant, kSet, kFirst);
        Virtual<void>(occupant, kSet, kSecond);
        return;
    }
    switch (*edges) {
    case 0x9:
    case 0x301:
    case 0x309:
    case 0xB00:
    case 0xB01:
        Virtual<void>(occupant, kSet, kCorner);
        Virtual<void>(occupant, kSet, kFirst);
        Virtual<void>(occupant, kClear, kSecond);
        return;
    case 0xB:
    case 0x100000B:
    case 0x1000003:
    case 0x9000000:
    case 0x9000003:
        Virtual<void>(occupant, kSet, kFirst);
        Virtual<void>(occupant, kClear, kSecond);
        return;
    case 0x900:
    case 0x30100:
    case 0x30900:
    case 0xB0000:
    case 0xB0100:
        Virtual<void>(occupant, kSet, kSecond);
        Virtual<void>(occupant, kClear, kFirst);
        return;
    case 0x90000:
    case 0x3010000:
    case 0x3090000:
    case 0xB000000:
    case 0xB010000:
        Virtual<void>(occupant, kSet, kCorner);
        Virtual<void>(occupant, kSet, kSecond);
        Virtual<void>(occupant, kClear, kFirst);
        return;
    default:
        Virtual<void>(occupant, kSet, kFirst);
        Virtual<void>(occupant, kSet, kSecond);
        return;
    }
}

// ====================================================================================
// Installation
// ====================================================================================

#define OPENSC4_DETOUR(address, fn) OPENSC4_NETTOOL_DETOUR(address, fn)

const Detour kCellPreviews[] = {
    OPENSC4_DETOUR(addr::cSC4NetworkTool_AddCellPreview, AddCellPreview),
    OPENSC4_DETOUR(addr::cSC4NetworkToolPreview_AddCell, AddCell),
    OPENSC4_DETOUR(addr::cSC4NetworkToolPreview_EraseCell, EraseCell),
    OPENSC4_DETOUR(addr::cSC4NetworkToolPreview_InsertCellNode, InsertCellNode),
    OPENSC4_DETOUR(addr::cSC4NetworkToolPreview_InsertTextured, InsertTextured),
    OPENSC4_DETOUR(addr::cSC4NetworkToolPreview_InsertTexturedNode, InsertTexturedNode),
    OPENSC4_DETOUR(addr::cSC4NetworkToolPreview_CreateTexturedNode, CreateTexturedNode),
};

const Detour kPieceMatch[] = {
    OPENSC4_DETOUR(addr::cSC4NetworkTool_FindPreviewPiece, FindPreviewPiece),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_NeighborConstruct, NeighborConstruct),
    OPENSC4_DETOUR(addr::SC4RuleFile_Match, RuleFileMatch),
    OPENSC4_DETOUR(addr::SC4RuleFile_EntryMatches, EntryMatches),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_NeighborEdgesCached, NeighborEdgesCached),
    OPENSC4_DETOUR(addr::SC4RuleCheckVector_InsertOverflow, CheckInsertOverflow),
    OPENSC4_DETOUR(addr::SC4RuleCheckVector_UninitializedCopy, CheckUninitializedCopy),
    OPENSC4_DETOUR(addr::SC4RuleCheckVector_UninitializedFillN, CheckUninitializedFillN),
};

const Detour kHighlights[] = {
    OPENSC4_DETOUR(addr::cSC4NetworkTool_UpdateOccupantHighlights, UpdateOccupantHighlights),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_DestroyHighlightedOccupants, DestroyHighlightedOccupants),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_CollectHighlights, CollectHighlights),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_DiffHighlights, DiffHighlights),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_SetQuadTrianglesToDraw, SetQuadTrianglesToDraw),
};

#undef OPENSC4_DETOUR

bool InstallCellPreviews(registry::Mode) { return InstallDetours(kCellPreviews, "cellpreviews"); }
bool InstallPieceMatch(registry::Mode) { return InstallDetours(kPieceMatch, "piecematch"); }
bool InstallHighlights(registry::Mode) { return InstallDetours(kHighlights, "highlights"); }

const bool g_registered[] = {
    registry::Register({"simulators.nettool.cellpreviews", "simulators/nettool", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4NetworkTool_AddCellPreview,
                        "Our network tool's cell previews but their drawing: slot 58 AddCellPreview and the "
                        "preview drawable's tree bookkeeping (the cell add and erase, the cell node insert, the "
                        "textured insert, node insert and node maker) (7 detours; src/core/nettool_preview.cpp)",
                        false, InstallCellPreviews}),
    registry::Register({"simulators.nettool.piecematch", "simulators/nettool", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4NetworkTool_FindPreviewPiece,
                        "Our network tool's rule match for a previewed cell's piece: the piece lookup, the "
                        "neighbour walker's constructor, the rule file's and the entry's match, the walker's "
                        "cached edges and the check vector's InsertOverflow, UninitializedCopy and "
                        "UninitializedFillN (8 detours; src/core/nettool_preview.cpp)",
                        false, InstallPieceMatch}),
    registry::Register({"simulators.nettool.highlights", "simulators/nettool", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4NetworkTool_UpdateOccupantHighlights,
                        "Our network tool's occupant highlights: slot 59 UpdateOccupantHighlights, slot 60 "
                        "DestroyHighlightedOccupants, the collector, the highlight set's diff and slot 80 "
                        "SetQuadTrianglesToDraw (5 detours; src/core/nettool_preview.cpp)",
                        false, InstallHighlights}),
};

} // namespace
} // namespace opensc4::nettool_sim
