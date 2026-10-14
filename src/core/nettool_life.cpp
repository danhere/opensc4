// Our cSC4NetworkTool, third part (build 641; the class map and the fields
// are in src/core/nettool_sim.h): the tool's life and its preview drawable.
// Two groups, each a list of detours at the game's addresses:
// simulators.nettool.life (the constructor, the destructor and the deleting
// one, Init, Shutdown, Reset, GetUITool, the message target's DoMessage, the
// two Clear slots 16 and 17 and the four helpers only the life code calls)
// and simulators.nettool.preview (the preview drawable's constructor,
// destructors, clear and tree helpers, and the release of its material).
//
// Every function was written from the disassembly. Calls between them go to
// the game's addresses (through the address entries), so each detour is
// independent of the others and OPENSC4_NETTOOL_SKIP bisects them.
//
// Left as the game's:
//   - the drawable's drawing: the render state setup 0x0061FD10 (Init calls
//     it at its address), its draw slot 0x00620500 and the batch drawing
//     0x00620160 under it. Drawing belongs to the Renderer thread.
//   - 0x00620810, the adjustor thunk of the drawable's part at +4 (`sub ecx,
//     4; jmp 0x00620BA0`): it jumps to our deleting destructor.
//
// NAM. None of these functions is guarded or called by NAM's DLL; NAM reads
// the tool object by layout, and these functions write every field in the
// game's memory with the game's layout (the object, the drawable, the
// trees, vectors and hash sets through the game's own allocator and
// template helpers). Nothing is kept outside the object.
//
// Floats: Init's +0x23C is the x87's FPTAN of the parameters' float +0x18
// (ST(0) the 1.0 popped) times the float +0x238, stored as a float; FPTAN
// has no C++ equivalent that rounds the same, so it is the same three
// instructions (X87TanTimes). Everything else moves words.
//
// The detours are plain functions, so they clobber ECX and EDX; the register
// scan of every game caller is in docs/nettool.md ("Parity"). AL is the
// answer where the game's is a byte (Init, Shutdown, GetUITool, DoMessage);
// EAX is the game's where callers could read it (the constructors and
// deleting destructors give the object, Reset the end of the emptied rule
// cell records as the game's last instruction leaves it).
//
// Calls, each count from the asm's pushes for that call and the callee's own
// `ret N`:
//   - the tool's: slot 13 ClearFailureReason (+0x34) none, slot 16 (+0x40)
//     and 17 (+0x44) none, slot 79 (+0x13C) one; the destructor 0x0062CAF0
//     and Shutdown 0x00626D30 none; CopyPlacementParams 0x00626860 one (ret
//     4); 0x0062F9D0 cdecl three; 0x0062C590 cdecl three; 0x0062BE40 and
//     0x00624F70 none;
//   - the world cache's constructor 0x00648000, destructor 0x00647AE0 and
//     Reset 0x00647B20 none; the satisfiers' constructor 0x00660810 and
//     destructor 0x006602C0 none; the hash set's construction 0x00829BC0 one
//     (ret 4) and Clear 0x008EBA60 none;
//   - the drawable's: its constructor 0x00620AD0 none, the render state
//     setup 0x0061FD10 none (AL), its release 0x0061FBC0 none, Clear
//     0x006208D0 none, the tree helpers 0x0061FCD0 and 0x006203A0 one (ret
//     4), the destructor 0x00620770 none; cRZBaseUnknown's construction
//     0x0090DA1E and destruction 0x0090D990 none; the texture binding's
//     release 0x008090E0 none; the tree rebalance 0x0040DAF0 cdecl four;
//   - the tool UI: its constructor 0x00645D50 none, 0x00645240 two (ret 8:
//     the tool and the type);
//   - the network occupant's pool 0x00619E90 cdecl one (the size, unread),
//     its constructor 0x0061BDD0 one (ret 4), the filter's 0x0061F510 none;
//   - shared template code: SC4RefSet::EraseSubtree 0x004676A0 one (ret 4),
//     SC4PtrVector::InsertN 0x0051CA60 five (ret 0x14), the record move
//     0x00624070 cdecl five; the STL allocator 0x0090CF54 and the free
//     0x0090CF63 cdecl one; operator new 0x005E55E0 and delete 0x005E5620
//     cdecl one; the rule loader 0x006253D0 none;
//   - the services: the app 0x00B43C94 slot 0x114 two (a callback, 0); the
//     render service 0x00B43DD0 slot 0x28 none, 0x80 three (the drawable,
//     the layer, the priority), 0x84 one; the message server slot 0x14 and
//     0x18 two (the target, the type); the city slot 0x174, 0x178 and 0x84
//     none; the terrain slot 0x18 none (a float in ST0); the occupant
//     manager 0x00B43D0C slot 0x54 three (the occupant, 1, 0); a cell
//     preview's slot 0x44 two (0, 0); a message's slot 0x10 and 0x3C none;
//     the temporary occupant's part at +4 slots 1 to 4 none.
#include "core/nettool_sim.h"

#include "core/game.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>

namespace opensc4::nettool_sim {
namespace {

// The game's free (0x0090CF63) and operator new and delete (0x005E55E0,
// 0x005E5620), all cdecl with one argument.
void Free(void* block)
{
    CallCdecl<void, void*>(addr::SC4_Free, block);
}
void* New(uint32_t size)
{
    return CallCdecl<void*, uint32_t>(addr::SC4_operator_new, size);
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

// A tree emptied as the game's inlined clear does: when it has nodes, the
// subtree eraser on the root, then the head linked to itself and the count
// cleared. `eraser` is the game's address of the tree's own (thiscall, the
// tree object and the root).
void ClearTree(char* object, std::uintptr_t eraser)
{
    if (!At<uint32_t>(object, tree::kCount))
        return;
    CallThis<void, char*>(eraser, object, At<char*>(At<char*>(object, tree::kHead), tree::kParent));
    char* const head = At<char*>(object, tree::kHead);
    At<char*>(head, tree::kLeft) = head;
    At<char*>(head, tree::kParent) = nullptr;
    At<char*>(head, tree::kRight) = head;
    At<uint32_t>(object, tree::kCount) = 0;
}

// A vector emptied as the game's inlined erase(begin, end) does (end =
// begin; nothing is moved).
void Empty(char* object, std::size_t vector)
{
    Vector& v = At<Vector>(object, vector);
    v.end = v.begin;
}

// Frees a pointer of the object when it is set (the destructors' pattern).
void FreeAt(char* object, std::size_t offset)
{
    if (void* const block = At<void*>(object, offset))
        Free(block);
}

// A head node of the game's tree (0x14 bytes from the STL allocator): colour
// 0, no parent, its own leftmost and rightmost; the object's head and count
// written as the game's constructors write them.
void ConstructTree(char* object, std::size_t at, std::size_t size)
{
    At<char*>(object, at + tree::kHead) = nullptr;
    char* const head = CallCdecl<char*, uint32_t>(addr::SC4_stl_allocate, static_cast<uint32_t>(size));
    At<char*>(object, at + tree::kHead) = head;
    At<uint32_t>(object, at + tree::kCount) = 0;
    At<uint8_t>(head, 0) = 0;
    At<char*>(At<char*>(object, at), tree::kParent) = nullptr;
    char* const again = At<char*>(object, at);
    At<char*>(again, tree::kLeft) = again;
    char* const third = At<char*>(object, at);
    At<char*>(third, tree::kRight) = third;
}

// FLD the parameters' float, FPTAN, FSTP ST(0), FMUL the terrain's float,
// FSTP float: Init's 0x00626C82 to 0x00626C92.
__declspec(naked) void __cdecl X87TanTimes(const float* /*angle*/, const float* /*factor*/, float* /*out*/)
{
    __asm mov eax, [esp + 4]
    __asm fld dword ptr [eax]
    __asm fptan
    __asm fstp st(0)
    __asm mov eax, [esp + 8]
    __asm fmul dword ptr [eax]
    __asm mov eax, [esp + 12]
    __asm fstp dword ptr [eax]
    __asm ret
}

// ====================================================================================
// The preview drawable (0x58 bytes; nettool_sim.h has its layout)
// ====================================================================================

// 0x0061FBC0 (no arguments; Shutdown): when the render state setup has run
// (+0x54), the material +0xC released and nulled, the flag cleared.
void __fastcall PreviewReleaseMaterial(char* self, void*)
{
    static bool logged = false;
    FirstCall("PreviewReleaseMaterial", logged);
    if (!At<uint8_t>(self, preview::kSetUp))
        return;
    if (void* const material = At<void*>(self, preview::kMaterial)) {
        At<void*>(self, preview::kMaterial) = nullptr;
        ReleaseOf(material);
    }
    At<uint8_t>(self, preview::kSetUp) = 0;
}

// 0x0061FCD0 (ret 4: a node; this the cell tree +0x10): the node's subtree
// freed, right subtrees by recursion, down the left children.
void __fastcall PreviewEraseCellTree(char* self, void*, char* node)
{
    static bool logged = false;
    FirstCall("PreviewEraseCellTree", logged);
    while (node) {
        CallThis<void, char*>(addr::cSC4NetworkToolPreview_EraseCellTree, self, At<char*>(node, tree::kRight));
        char* const left = At<char*>(node, tree::kLeft);
        Free(node);
        node = left;
    }
}

// 0x006203A0 (ret 4: a node; this the textured tree +0x1C): the same, each
// node's texture binding (+0x7C) released first when it has one.
void __fastcall PreviewEraseTexturedTree(char* self, void*, char* node)
{
    static bool logged = false;
    FirstCall("PreviewEraseTexturedTree", logged);
    while (node) {
        CallThis<void, char*>(addr::cSC4NetworkToolPreview_EraseTexturedTree, self,
                              At<char*>(node, tree::kRight));
        void* const texture = At<void*>(node, preview::kTexture);
        char* const left = At<char*>(node, tree::kLeft);
        if (texture)
            CallThis<uint32_t>(addr::SC4TextureBinding_Release, texture);
        Free(node);
        node = left;
    }
}

// The two range erasers (ret 8: first, last; this a tree): the whole tree
// when the range is all of it, else node by node through the rebalance
// 0x0040DAF0, the count down by one each. `textured` releases each node's
// texture binding first (0x006206A0) or not (0x006202E0).
void EraseRange(char* self, char* first, char* last, bool textured, std::uintptr_t eraser)
{
    char* head = At<char*>(self, tree::kHead);
    if (first == At<char*>(head, tree::kLeft) && last == head) {
        if (!At<uint32_t>(self, tree::kCount))
            return;
        CallThis<void, char*>(eraser, self, At<char*>(head, tree::kParent));
        head = At<char*>(self, tree::kHead);
        At<char*>(head, tree::kLeft) = head;
        At<char*>(At<char*>(self, tree::kHead), tree::kParent) = nullptr;
        head = At<char*>(self, tree::kHead);
        At<char*>(head, tree::kRight) = head;
        At<uint32_t>(self, tree::kCount) = 0;
        return;
    }
    while (first != last) {
        char* const node = first;
        first = Next(first);
        head = At<char*>(self, tree::kHead);
        char* const freed = CallCdecl<char*, char*, char**, char**, char**>(
            addr::SC4Tree_RebalanceForErase, node, &At<char*>(head, tree::kParent), &At<char*>(head, tree::kLeft),
            &At<char*>(head, tree::kRight));
        if (textured) {
            if (void* const texture = At<void*>(freed, preview::kTexture))
                CallThis<uint32_t>(addr::SC4TextureBinding_Release, texture);
        }
        if (freed)
            Free(freed);
        --At<uint32_t>(self, tree::kCount);
    }
}

void __fastcall PreviewEraseCellRange(char* self, void*, char* first, char* last)
{
    static bool logged = false;
    FirstCall("PreviewEraseCellRange", logged);
    EraseRange(self, first, last, false, addr::cSC4NetworkToolPreview_EraseCellTree);
}

void __fastcall PreviewEraseTexturedRange(char* self, void*, char* first, char* last)
{
    static bool logged = false;
    FirstCall("PreviewEraseTexturedRange", logged);
    EraseRange(self, first, last, true, addr::cSC4NetworkToolPreview_EraseTexturedTree);
}

// 0x00620AD0 (no arguments; Init): the first vtable, the cRZBaseUnknown part
// at +4 constructed (0x0090DA1E), the final vtables, the material cleared,
// the two trees' head nodes (0x7C and 0x80 bytes), the four render states
// cleared and the state bytes; EAX this.
char* __fastcall PreviewConstruct(char* self, void*)
{
    static bool logged = false;
    FirstCall("PreviewConstruct", logged);
    At<std::uintptr_t>(self, 0) = life::kPreviewFirstVtable;
    CallThis<void*>(addr::cRZBaseUnknown_Construct, self + preview::kBase);
    At<std::uintptr_t>(self, 0) = addr::cSC4NetworkToolPreview_vftable;
    At<std::uintptr_t>(self, preview::kBase) = addr::cSC4NetworkToolPreview_BaseVftable;
    At<void*>(self, preview::kMaterial) = nullptr;
    ConstructTree(self, preview::kCells, preview::kCellNodeSize);
    ConstructTree(self, preview::kTextured, preview::kTexturedNodeSize);
    for (std::size_t at = preview::kStates; at < preview::kStatesEnd; at += 4)
        At<uint32_t>(self, at) = 0;
    const uint8_t bytes[] = {1, 0, 3, 1, 1, 0, 3, 1, 0, 0, 0, 0, 0}; // +0x48 to +0x54
    for (std::size_t i = 0; i < sizeof bytes; ++i)
        At<uint8_t>(self, preview::kStatesEnd + i) = bytes[i];
    return self;
}

// 0x00620770 (no arguments; the deleting destructor 0x00620BA0): the final
// vtables written back, the textured tree emptied and its head freed, the
// cell tree the same, the material released (not nulled), then the
// cRZBaseUnknown part's destructor (0x0090D990, a tail jump in the game's).
void __fastcall PreviewDestruct(char* self, void*)
{
    static bool logged = false;
    FirstCall("PreviewDestruct", logged);
    At<std::uintptr_t>(self, 0) = addr::cSC4NetworkToolPreview_vftable;
    At<std::uintptr_t>(self, preview::kBase) = addr::cSC4NetworkToolPreview_BaseVftable;
    ClearTree(self + preview::kTextured, addr::cSC4NetworkToolPreview_EraseTexturedTree);
    FreeAt(self, preview::kTextured + tree::kHead);
    ClearTree(self + preview::kCells, addr::cSC4NetworkToolPreview_EraseCellTree);
    FreeAt(self, preview::kCells + tree::kHead);
    if (void* const material = At<void*>(self, preview::kMaterial))
        ReleaseOf(material);
    CallThis<void>(addr::cRZBaseUnknown_Destruct, self + preview::kBase);
}

// 0x006208D0 (no arguments; ClearOccupantHighlights): both trees emptied,
// the cell tree first; the heads kept.
void __fastcall PreviewClear(char* self, void*)
{
    static bool logged = false;
    FirstCall("PreviewClear", logged);
    ClearTree(self + preview::kCells, addr::cSC4NetworkToolPreview_EraseCellTree);
    ClearTree(self + preview::kTextured, addr::cSC4NetworkToolPreview_EraseTexturedTree);
}

// 0x00620BA0 (ret 4: flags; slot 2 of the vtable 0x00AA8314 and, through
// the thunk 0x00620810, of the part at +4): the destructor at its address,
// operator delete when bit 0 is set; EAX this.
char* __fastcall PreviewDeletingDestructor(char* self, void*, uint32_t flags)
{
    static bool logged = false;
    FirstCall("PreviewDeletingDestructor", logged);
    CallThis<void>(addr::cSC4NetworkToolPreview_Destruct, self);
    if (flags & 1)
        Delete(self);
    return self;
}

// ====================================================================================
// The tool's life
// ====================================================================================

// 0x00624F70 (no arguments; this the placement parameters, the tool's +0x290;
// also called by 0x00643510 and 0x00645E90): the five vectors' memory freed,
// +0xA0, +0x6C, +0x60, +0x54, +0x28 in that order.
void __fastcall DestroyPlacementParams(char* self, void*)
{
    static bool logged = false;
    FirstCall("DestroyPlacementParams", logged);
    for (const std::size_t vector : {params::kVectorA0, params::kFloats6C, params::kVector60, params::kVector54,
                                     params::kVector28})
        FreeAt(self, vector);
}

// 0x0062BE40 (no arguments; this the intersections' vector +0xCC; also
// called by 0x0062BFE0): each 0x28-byte element's pointer +0x1C freed, then
// the elements.
void __fastcall DestroyIntersections(char* self, void*)
{
    static bool logged = false;
    FirstCall("DestroyIntersections", logged);
    const Vector& v = At<Vector>(self, 0);
    char* const end = v.end;
    for (char* at = v.begin; at != end; at += 0x28)
        FreeAt(at, 0x1C);
    if (v.begin)
        Free(v.begin);
}

// 0x0062C590 (cdecl: first, last, an unread tag; the major bridges' range):
// for each 0x28-byte element, each 0x50-byte entry of its vector +0x1C has
// its pointer +0x44 freed, then the vector's memory.
void __cdecl DestroyMajorBridges(char* first, char* last, void*)
{
    static bool logged = false;
    FirstCall("DestroyMajorBridges", logged);
    for (char* at = first; at != last; at += 0x28) {
        char* const end = At<char*>(at, 0x20);
        for (char* entry = At<char*>(at, 0x1C); entry != end; entry += 0x50)
            FreeAt(entry, 0x44);
        FreeAt(at, 0x1C);
    }
}

// A uint32 appended as the game's inlined push_back does: in place while
// there is room (nothing written when the end is null), else through
// SC4PtrVector::InsertN 0x0051CA60 (where, the value's address, an unread
// tag, 1, 1).
void PushBack(Vector& v, uint32_t value)
{
    if (v.end != v.capacity) {
        if (v.end)
            *reinterpret_cast<uint32_t*>(v.end) = value;
        v.end += 4;
        return;
    }
    uint8_t tag = 0;
    CallThis<void, char*, const uint32_t*, void*, uint32_t, uint32_t>(addr::SC4PtrVector_InsertN, &v, v.end, &value,
                                                                     &tag, 1u, 1u);
}

// 0x0062F9D0 (cdecl: the type row's bridge records, the vector +0x33C and
// the vector +0x348; Init): both vectors emptied, then for each record (0x178
// bytes) its index appended to +0x348 when its byte +0x98 is set and to
// +0x33C when its byte +0x99 is.
void __cdecl ListBridgeKinds(const Vector* records, Vector* by99, Vector* by98)
{
    static bool logged = false;
    FirstCall("ListBridgeKinds", logged);
    by99->end = by99->begin;
    by98->end = by98->begin;
    if (!records)
        return;
    const int32_t count = static_cast<int32_t>(records->end - records->begin) / static_cast<int32_t>(bridge::kSize);
    for (int32_t i = 0; i < count; ++i) {
        char* const record = records->begin + static_cast<std::size_t>(i) * bridge::kSize;
        if (At<uint8_t>(record, bridge_kind::kByte98))
            PushBack(*by98, static_cast<uint32_t>(i));
        if (At<uint8_t>(record, bridge_kind::kByte99))
            PushBack(*by99, static_cast<uint32_t>(i));
    }
}

// 0x0062C5F0 (ret 4: the network type; the network manager's Init and
// GetNetworkTool, 0x00612820 and the pipe, power line and underground
// constructors): the fields as the game's writes them, in its order; the
// failure reason +0x20C, +0x51 and the words +0x6C to +0x80 are not written.
// EAX this.
char* __fastcall Construct(char* self, void*, uint32_t type)
{
    static bool logged = false;
    FirstCall("Construct", logged);
    At<std::uintptr_t>(self, kMessagePart) = life::kToolFirstMessageVtable;
    At<std::uintptr_t>(self, 0) = addr::cSC4NetworkTool_vftable;
    At<std::uintptr_t>(self, kMessagePart) = kMessageVtable;
    for (const std::size_t word : {0x8u, 0xCu, 0x10u, 0x14u})
        At<uint32_t>(self, word) = 0;
    At<uint8_t>(self, field::kInitialized) = 0;
    CallThis<void>(addr::cSC4NetworkWorldCache_Construct, self + field::kWorldCache);
    At<void*>(self, field::kTerrain) = nullptr;
    At<void*>(self, field::kOccupants) = nullptr;
    At<void*>(self, field::kCityObject4C) = nullptr;
    At<uint8_t>(self, field::kReplacing) = 0;
    At<uint8_t>(self, field::kByte52) = 1;
    At<uint8_t>(self, field::kReload) = 0;
    for (std::size_t at = field::kSteps; at <= 0x68; at += 4)
        At<uint32_t>(self, at) = 0;
    // The three hash sets: their words +0x4 to +0x10, then 100 buckets.
    for (const std::size_t set : {field::kSet84, field::kSetA4, field::kSetB8}) {
        for (std::size_t at = set + 4; at <= set + 0x10; at += 4)
            At<uint32_t>(self, at) = 0;
        CallThis<void, uint32_t>(addr::SC4Uint32HashSet_Construct, self + set, 100u);
        if (set == field::kSet84)
            for (const std::size_t word : {field::kSolved, field::kSolved + 4, field::kSolved + 8})
                At<uint32_t>(self, word) = 0;
    }
    for (std::size_t at = field::kIntersections; at <= 0x120; at += 4)
        At<uint32_t>(self, at) = 0;
    CallThis<void>(addr::SC4VertexHeightSatisfier_Construct, self + field::kSatisfier1);
    CallThis<void>(addr::SC4VertexHeightSatisfier_Construct, self + field::kSatisfier2);
    At<uint32_t>(self, field::kOtherSurfaceMask) =
        ~Global<uint32_t>(addr::cSC4NetworkTool_TypeFlags + type * 4) & kSurfaceMask;
    for (std::size_t at = field::kPreview; at <= 0x1FC; at += 4)
        At<uint32_t>(self, at) = 0;
    At<int32_t>(self, field::kPreferredBridge) = kNoPreferredBridge;
    At<uint32_t>(self, field::kAltitudeOverride) = kNoAltitudeOverrideBits;
    At<uint32_t>(self, field::kWord208) = 1;
    At<void*>(self, life::kPreviewList) = nullptr;
    ConstructTree(self, life::kCellPreviews, life::kTreeNodeSize);
    At<uint32_t>(self, 0x220) = 0;
    At<uint32_t>(self, 0x224) = 0;
    ConstructTree(self, life::kHighlights, life::kTreeNodeSize);
    At<uint32_t>(self, field::kCellsX) = 0;
    At<uint32_t>(self, field::kCellsZ) = 0;
    At<uint8_t>(self, field::kPreviewUpdate) = 1;
    for (const std::size_t word : {0x2B8u, 0x2BCu, 0x2C0u})
        At<uint32_t>(self, word) = 0;
    for (std::size_t at = 0x2E4; at <= 0x304; at += 4)
        At<uint32_t>(self, at) = 0;
    for (std::size_t at = 0x330; at <= 0x350; at += 4)
        At<uint32_t>(self, at) = 0;
    At<uint32_t>(self, field::kType) = type;
    At<uint8_t>(self, field::kHasBridge) = 0;
    At<void*>(self, field::kRow) = nullptr;
    return self;
}

// 0x00626D30 (slot 4; also the destructor's): when initialised, the message
// 0xA6B79602 unsubscribed, the possible bridges emptied, slot 17, the
// anchors and the dragged cells emptied, slot 16, the city object +0x4C, the
// occupant manager +0x48, the terrain +0x44 and the filter +0x364 released
// and nulled, the drawable unregistered from the render service, its
// material released (0x0061FBC0) and the drawable released, +0x18 cleared.
// AL 1 always.
uint8_t __fastcall Shutdown(char* self, void*)
{
    static bool logged = false;
    FirstCall("Shutdown", logged);
    if (!At<uint8_t>(self, field::kInitialized))
        return 1;
    if (void* const server = Global<void*>(addr::SC4_MessageServer))
        Virtual<uint8_t>(server, 0x18, self + kMessagePart, life::kRefreshMessage);
    Empty(self, field::kPossibleBridges);
    Virtual<void>(self, slot::kClearCellPreviews);
    Empty(self, field::kAnchors);
    Empty(self, field::kCells);
    Virtual<void>(self, slot::kClearOccupantHighlights);
    for (const std::size_t held : {field::kCityObject4C, field::kOccupants, field::kTerrain, field::kFilter}) {
        if (void* const object = At<void*>(self, held)) {
            ReleaseOf(object);
            At<void*>(self, held) = nullptr;
        }
    }
    if (char* const drawable = At<char*>(self, field::kPreview)) {
        Virtual<void>(Global<void*>(addr::SC4_View43DD0), 0x84, drawable);
        CallThis<void>(addr::cSC4NetworkToolPreview_ReleaseMaterial, At<char*>(self, field::kPreview));
        if (char* const again = At<char*>(self, field::kPreview)) {
            At<char*>(self, field::kPreview) = nullptr;
            ReleaseOf(again);
        }
    }
    At<uint8_t>(self, field::kInitialized) = 0;
    return 1;
}

// 0x006269C0 (slot 3; the power line and underground tools' Init call it at
// its address): once until Shutdown. See the top of nettool_sim.h for what
// each field is; the order is the game's.
uint8_t __fastcall Init(char* self, void*)
{
    static bool logged = false;
    FirstCall("Init", logged);
    if (At<uint8_t>(self, field::kInitialized))
        return 1;
    const uint32_t type = At<uint32_t>(self, field::kType);
    At<std::uintptr_t>(self, field::kRow) = addr::cSC4NetworkConstructionCrew_NetworkToolTuning + type * kRowSize;
    // The first tool after a resource load: the app gets the callback that
    // clears the flag again, and the rule files are loaded.
    if (!Global<uint8_t>(addr::cSC4NetworkTool_RulesLoaded)) {
        void* const app = Global<void*>(addr::SC4_Service43C94);
        Global<uint8_t>(addr::cSC4NetworkTool_RulesLoaded) = 1;
        Virtual<void>(app, 0x114, addr::cSC4NetworkTool_RulesResetCallback, 0u);
        CallCdecl<void>(addr::cSC4NetworkTool_LoadRules);
    }
    Virtual<void>(self, 0x13C,
                  Global<uint32_t>(addr::cSC4NetworkTool_SpecificInfo + At<uint32_t>(self, field::kType) * 4));
    // The preview drawable, assigned as a reference.
    void* const memory = New(preview::kSize);
    char* const fresh = memory ? CallThis<char*>(addr::cSC4NetworkToolPreview_Construct, memory) : nullptr;
    char* const old = At<char*>(self, field::kPreview);
    if (fresh != old) {
        if (fresh)
            AddRefOf(fresh);
        At<char*>(self, field::kPreview) = fresh;
        if (old)
            ReleaseOf(old);
    }
    CallThis<uint8_t>(addr::cSC4NetworkToolPreview_SetupRenderState, At<char*>(self, field::kPreview));
    Virtual<void>(Global<void*>(addr::SC4_View43DD0), 0x80, At<char*>(self, field::kPreview), life::kPreviewLayer,
                  life::kPreviewPriority);
    // The class mask and its filter.
    At<uint32_t>(self, field::kClassMask) = kSurfaceMask;
    const uint32_t flag = Global<uint32_t>(addr::cSC4NetworkTool_TypeFlags + At<uint32_t>(self, field::kType) * 4);
    if (!(flag & kSurfaceMask))
        At<uint32_t>(self, field::kClassMask) = flag;
    void* const filterMemory = New(life::kFilterSize);
    char* const filter =
        filterMemory ? CallThis<char*>(addr::cSC4NetworkManager_NetworkFilterConstructDefault, filterMemory) : nullptr;
    At<char*>(self, field::kFilter) = filter;
    AddRefOf(filter);
    At<uint32_t>(At<char*>(self, field::kFilter), 0xC) = At<uint32_t>(self, field::kClassMask);
    // The placement parameters and the bridge records.
    CallThis<void, const char*>(addr::cSC4NetworkTool_CopyPlacementParams, self + field::kParams,
                                At<char*>(self, field::kRow) + row::kParams);
    CallCdecl<void, char*, char*, char*>(addr::cSC4NetworkTool_ListBridgeKinds, At<char*>(self, field::kRow) + row::kBridges,
                                         self + life::kBridgesBy99, self + life::kBridgesBy98);
    At<uint8_t>(self, field::kHasBridge) = 0;
    {
        // The game tests the address of each record's byte +0x98, not the
        // byte: any record sets the flag.
        const char* const typeRow = At<char*>(self, field::kRow);
        const Vector& records = At<Vector>(const_cast<char*>(typeRow), row::kBridges);
        uint32_t count = static_cast<uint32_t>(static_cast<int32_t>(records.end - records.begin) /
                                               static_cast<int32_t>(bridge::kSize));
        uint32_t offset = 0;
        for (uint32_t i = 0; i < count; ++i, offset += bridge::kSize) {
            if (reinterpret_cast<std::uintptr_t>(At<char*>(const_cast<char*>(typeRow), row::kBridges)) + offset +
                    bridge_kind::kByte98 != 0)
                At<uint8_t>(self, field::kHasBridge) = 1;
            const Vector& now = At<Vector>(At<char*>(self, field::kRow), row::kBridges);
            count = static_cast<uint32_t>(static_cast<int32_t>(now.end - now.begin) /
                                          static_cast<int32_t>(bridge::kSize));
        }
    }
    // The city's size and the vertex steps.
    void* const city = Global<void*>(addr::SC4_City);
    const uint32_t cellsX = Virtual<uint32_t>(city, 0x174);
    At<uint32_t>(self, field::kCellsX) = cellsX;
    At<uint32_t>(self, life::kVerticesX) = cellsX + 1;
    const uint32_t cellsZ = Virtual<uint32_t>(city, 0x178);
    At<uint32_t>(self, field::kCellsZ) = cellsZ;
    At<uint32_t>(self, life::kVerticesZ) = cellsZ + 1;
    {
        const uint32_t row1 = At<uint32_t>(self, life::kVerticesX), row2 = row1 + 1;
        const uint32_t steps[16] = {row2, row1, row1, 0, 0, 1, 1, row2, 1, 1, row2, row2, row1, row1, 0, 0};
        for (std::size_t i = 0; i < 16; ++i)
            At<uint32_t>(self, life::kVertexSteps + 4 * i) = steps[i];
    }
    // The services, each referenced.
    void* const terrain = Global<void*>(addr::SC4_Terrain);
    At<void*>(self, field::kTerrain) = terrain;
    if (terrain) {
        AddRefOf(terrain);
        At<float>(self, life::kTerrainScale) = Virtual<float>(At<void*>(self, field::kTerrain), 0x18);
    }
    void* const occupants = Global<void*>(addr::SC4_NetworkService);
    At<void*>(self, field::kOccupants) = occupants;
    if (occupants)
        AddRefOf(occupants);
    void* const object4C = Virtual<void*>(city, 0x84);
    At<void*>(self, field::kCityObject4C) = object4C;
    if (object4C)
        AddRefOf(object4C);
    X87TanTimes(&At<float>(self, life::kAngle), &At<float>(self, life::kTerrainScale),
                &At<float>(self, life::kTerrainTan));
    uint32_t refresh = 0;
    if (void* const render = Global<void*>(addr::SC4_View43DD0))
        if (char* const state = Virtual<char*>(render, 0x28))
            refresh = At<uint32_t>(state, 0x14);
    At<uint32_t>(self, field::kRefreshWord) = refresh;
    // A network occupant made, initialised, shut down and released.
    void* const pooled = CallCdecl<void*, uint32_t>(addr::cSC4NetworkOccupant_Allocate, life::kTempOccupantSize);
    char* part = nullptr;
    if (pooled)
        if (char* const occupant = CallThis<char*, uint32_t>(addr::cSC4NetworkOccupant_cSC4NetworkOccupant, pooled, 1u))
            part = occupant + 4;
    if (part)
        AddRefOf(part);
    Virtual<uint8_t>(part, 0xC);
    Virtual<uint8_t>(part, 0x10);
    ReleaseOf(part);
    if (void* const server = Global<void*>(addr::SC4_MessageServer))
        Virtual<uint8_t>(server, 0x14, self + kMessagePart, life::kRefreshMessage);
    At<uint8_t>(self, field::kInitialized) = 1;
    return 1;
}

// 0x00626F90 (slot 5; the general, underground, pipe, subway and power line
// tools): the world cache emptied, slots 17 and 16, the anchors and the
// dragged cells emptied, the bridge settings back to none, the possible
// bridges emptied, slot 13, the rule cell records emptied through the
// record move 0x00624070 (end, end, begin; its answer, the begin, is the
// new end and EAX).
char* __fastcall Reset(char* self, void*)
{
    static bool logged = false;
    FirstCall("Reset", logged);
    CallThis<void>(addr::cSC4NetworkWorldCache_Reset, self + field::kWorldCache);
    Virtual<void>(self, slot::kClearCellPreviews);
    Virtual<void>(self, slot::kClearOccupantHighlights);
    Empty(self, field::kAnchors);
    Empty(self, field::kCells);
    At<int32_t>(self, field::kPreferredBridge) = kNoPreferredBridge;
    At<uint32_t>(self, field::kAltitudeOverride) = kNoAltitudeOverrideBits;
    Empty(self, field::kPossibleBridges);
    Virtual<void>(self, slot::kClearFailureReason);
    Vector& records = At<Vector>(self, life::kRuleCells);
    uint8_t tag = 0;
    char* const end = CallCdecl<char*, char*, char*, char*, uint8_t*, uint32_t>(
        addr::SC4LocationVector_Move, records.end, records.end, records.begin, &tag, 0u);
    records.end = end;
    return end;
}

// 0x00623B20 (slot 6, ret 4: where the UI object goes): a new tool UI object
// (0x134 bytes, 0x00645D50), referenced, attached to this tool and its type
// (0x00645240), given to the caller; AL 1.
uint8_t __fastcall GetUITool(char* self, void*, void** out)
{
    static bool logged = false;
    FirstCall("GetUITool", logged);
    void* const memory = New(life::kUIToolSize);
    char* const ui = memory ? CallThis<char*>(addr::cSC4NetworkToolUI_Construct, memory) : nullptr;
    AddRefOf(ui);
    CallThis<uint8_t, char*, uint32_t>(addr::cSC4NetworkToolUI_Attach, ui, self, At<uint32_t>(self, field::kType));
    *out = ui;
    return 1;
}

// 0x00624480 (the message target's slot 3, ret 4: the message; this the
// part at +4 of every tool of the family): the message 0xA6B79602's first
// data word kept at the tool's +0xFC; AL 1.
uint8_t __fastcall DoMessage(char* part, void*, void* message)
{
    static bool logged = false;
    FirstCall("DoMessage", logged);
    if (Virtual<uint32_t>(message, 0x10) == life::kRefreshMessage)
        At<uint32_t>(part, field::kRefreshWord - kMessagePart) = *Virtual<uint32_t*>(message, 0x3C);
    return 1;
}

// 0x00625580 (slot 16): the drawable's trees emptied (0x006208D0, the
// drawable not tested for null), every highlighted occupant given to the
// occupant manager's slot 0x54 (the occupant, 1, 0) and released, then the
// tree emptied (SC4RefSet::EraseSubtree releases each again, as the game's).
void __fastcall ClearOccupantHighlights(char* self, void*)
{
    static bool logged = false;
    FirstCall("ClearOccupantHighlights", logged);
    CallThis<void>(addr::cSC4NetworkToolPreview_Clear, At<char*>(self, field::kPreview));
    char* const head = At<char*>(self, life::kHighlights);
    void* const occupants = Global<void*>(addr::SC4_NetworkService);
    for (char* node = At<char*>(head, tree::kLeft); node != head;) {
        void* const occupant = At<void*>(node, tree::kValue);
        Virtual<void>(occupants, 0x54, occupant, 1u, 0u);
        ReleaseOf(occupant);
        node = Next(node);
    }
    ClearTree(self + life::kHighlights, addr::SC4RefSet_EraseSubtree);
}

// 0x00625F10 (slot 17; the general and the power line tool): every cell
// preview's slot 0x44 with two zeros, the tree emptied (EraseSubtree), then
// the preview list +0x210, when there is one, freed with its objects.
void __fastcall ClearCellPreviews(char* self, void*)
{
    static bool logged = false;
    FirstCall("ClearCellPreviews", logged);
    for (char* node = At<char*>(At<char*>(self, life::kCellPreviews), tree::kLeft);
         node != At<char*>(self, life::kCellPreviews); node = Next(node))
        Virtual<void>(At<void*>(node, tree::kValue), 0x44, 0u, 0u);
    ClearTree(self + life::kCellPreviews, addr::SC4RefSet_EraseSubtree);
    char* const list = At<char*>(self, life::kPreviewList);
    if (!list)
        return;
    At<std::uintptr_t>(list, 0x10) = life::kPreviewListVtable;
    if (At<void**>(list, 0x20)) {
        for (uint32_t i = 0; i < At<uint32_t>(list, 0x14); ++i)
            Delete(At<void**>(list, 0x20)[i]);
        Delete(At<void*>(list, 0x20));
    }
    Delete(list);
    At<void*>(self, life::kPreviewList) = nullptr;
}

// 0x0062CAF0 (no arguments; the deleting destructor 0x0062CD30 and the underground tools' destructor
// 0x00658250): the vtables back, Shutdown at its address, then every
// container's memory freed in the game's order and the world cache
// destroyed. The drawable is released when Shutdown left one (it never does
// after an Init).
void __fastcall Destruct(char* self, void*)
{
    static bool logged = false;
    FirstCall("Destruct", logged);
    At<std::uintptr_t>(self, 0) = addr::cSC4NetworkTool_vftable;
    At<std::uintptr_t>(self, kMessagePart) = kMessageVtable;
    CallThis<uint8_t>(addr::cSC4NetworkTool_Shutdown, self);
    FreeAt(self, life::kBridgesBy98);
    FreeAt(self, life::kBridgesBy99);
    CallThis<void>(addr::cSC4NetworkTool_DestroyPlacementParams, self + field::kParams);
    for (const std::size_t at : {life::kHighlights, life::kCellPreviews}) {
        ClearTree(self + at, addr::SC4RefSet_EraseSubtree);
        FreeAt(self, at + tree::kHead);
    }
    FreeAt(self, field::kPossibleBridges);
    if (void* const drawable = At<void*>(self, field::kPreview))
        ReleaseOf(drawable);
    CallThis<void>(addr::SC4VertexHeightSatisfier_Destruct, self + field::kSatisfier2);
    CallThis<void>(addr::SC4VertexHeightSatisfier_Destruct, self + field::kSatisfier1);
    for (const std::size_t vector : {field::kAnchors, field::kNeighborConnections, life::kVector100,
                                     life::kRuleCells, life::kVectorE4})
        FreeAt(self, vector);
    uint8_t tag = 0;
    CallCdecl<void, char*, char*, uint8_t*>(addr::cSC4NetworkTool_DestroyMajorBridges,
                                            At<char*>(self, field::kMajorBridges),
                                            At<char*>(self, field::kMajorBridges + 4), &tag);
    FreeAt(self, field::kMajorBridges);
    CallThis<void>(addr::cSC4NetworkTool_DestroyIntersections, self + field::kIntersections);
    for (const std::size_t set : {field::kSetB8, field::kSetA4}) {
        CallThis<void>(addr::SC4Uint32HashSet_Clear, self + set);
        FreeAt(self, set + 4);
    }
    FreeAt(self, field::kSolved);
    CallThis<void>(addr::SC4Uint32HashSet_Clear, self + field::kSet84);
    FreeAt(self, field::kSet84 + 4);
    FreeAt(self, field::kCells);
    FreeAt(self, field::kSteps);
    CallThis<void>(addr::cSC4NetworkWorldCache_Destruct, self + field::kWorldCache);
    FreeAt(self, life::kFreedPointer);
}

// 0x0062CD30 (slot 51, ret 4: flags): the destructor at its address,
// operator delete when bit 0 is set; EAX this.
char* __fastcall DeletingDestructor(char* self, void*, uint32_t flags)
{
    static bool logged = false;
    FirstCall("DeletingDestructor", logged);
    CallThis<void>(addr::cSC4NetworkTool_Destruct, self);
    if (flags & 1)
        Delete(self);
    return self;
}

// ====================================================================================
// Installation
// ====================================================================================

#define OPENSC4_DETOUR(address, fn) OPENSC4_NETTOOL_DETOUR(address, fn)

const Detour kLife[] = {
    OPENSC4_DETOUR(addr::cSC4NetworkManager_NetworkToolConstruct, Construct),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_Destruct, Destruct),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_DeletingDestructor, DeletingDestructor),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_Init, Init),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_Shutdown, Shutdown),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_Reset, Reset),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_GetUITool, GetUITool),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_DoMessage, DoMessage),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_ClearOccupantHighlights, ClearOccupantHighlights),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_ClearCellPreviews, ClearCellPreviews),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_DestroyPlacementParams, DestroyPlacementParams),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_DestroyIntersections, DestroyIntersections),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_DestroyMajorBridges, DestroyMajorBridges),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_ListBridgeKinds, ListBridgeKinds),
};

const Detour kPreview[] = {
    OPENSC4_DETOUR(addr::cSC4NetworkToolPreview_ReleaseMaterial, PreviewReleaseMaterial),
    OPENSC4_DETOUR(addr::cSC4NetworkToolPreview_EraseCellTree, PreviewEraseCellTree),
    OPENSC4_DETOUR(addr::cSC4NetworkToolPreview_EraseCellRange, PreviewEraseCellRange),
    OPENSC4_DETOUR(addr::cSC4NetworkToolPreview_EraseTexturedTree, PreviewEraseTexturedTree),
    OPENSC4_DETOUR(addr::cSC4NetworkToolPreview_EraseTexturedRange, PreviewEraseTexturedRange),
    OPENSC4_DETOUR(addr::cSC4NetworkToolPreview_Construct, PreviewConstruct),
    OPENSC4_DETOUR(addr::cSC4NetworkToolPreview_Destruct, PreviewDestruct),
    OPENSC4_DETOUR(addr::cSC4NetworkToolPreview_Clear, PreviewClear),
    OPENSC4_DETOUR(addr::cSC4NetworkToolPreview_DeletingDestructor, PreviewDeletingDestructor),
};

#undef OPENSC4_DETOUR

bool InstallLife(registry::Mode) { return InstallDetours(kLife, "life"); }
bool InstallPreview(registry::Mode) { return InstallDetours(kPreview, "preview"); }

const bool g_registered[] = {
    registry::Register({"simulators.nettool.life", "simulators/nettool", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4NetworkTool_Init,
                        "Our network tool's life: its constructor, destructor and deleting destructor, Init, "
                        "Shutdown, Reset, GetUITool, the message target's DoMessage, ClearOccupantHighlights, "
                        "ClearCellPreviews and the four helpers of the life code (14 detours; "
                        "src/core/nettool_life.cpp)",
                        false, InstallLife}),
    registry::Register({"simulators.nettool.preview", "simulators/nettool", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4NetworkToolPreview_Construct,
                        "Our network tool's preview drawable but its drawing: its constructor, destructor and "
                        "deleting destructor, its clear, the release of its material and its four tree helpers (9 "
                        "detours; src/core/nettool_life.cpp)",
                        false, InstallPreview}),
};

} // namespace
} // namespace opensc4::nettool_sim
