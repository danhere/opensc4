// Our overlay manager's bookkeeping, part 3: the set-up and the containers
// (src/core/overlay_manage_3.cpp for ours, overlay_manage_3_hooks.cpp for
// the detours and compare mode; docs/renderer.md, "Bookkeeping").
#pragma once
#include <cstddef>
#include <cstdint>

namespace opensc4::overlay_manage::part3 {

// The manager (the layout part 2 uses, overlay_manage_2.h, and what the
// set-up writes).
constexpr std::size_t kSize = 0xFC;
constexpr std::size_t kBase = 0x8, kTarget = 0x10;  // cRZBaseUnknown, the message target
constexpr std::size_t kStarted = 0x14, kWaterRows = 0x15, kWaterVertices = 0x16;
constexpr std::size_t kShadowStages = 0x18, kDetail = 0x1C, kInView = 0x20;
constexpr std::size_t kStates = 0x24;               // a vector of five 8-byte render states
constexpr std::size_t kCellsAcross = 0x5C, kCellsDown = 0x60, kCellSize = 0x64, kCellScale = 0x68;
constexpr std::size_t kExtent = 0x6C;               // float[3]: the terrain's size across, across, down
constexpr std::size_t kClip = 0x78;                 // int32[4]
constexpr std::size_t kTerrain = 0x88, kView = 0x8C, kTextures = 0x90;
constexpr std::size_t kOverlays = 0x98, kHandles = 0xAC, kGroups = 0xC0;
constexpr std::size_t kShadowTexture = 0xD4;
constexpr std::size_t kStamp = 0xE0, kBucketShift = 0xE4, kBucketsAcross = 0xE8, kBucketsDown = 0xEC, kBuckets = 0xF0;
constexpr std::size_t kEnd = 0x4, kCapacity = 0x8, kFirstFree = 0xC, kLastFree = 0x10;
constexpr std::size_t kOverlaySize = 0xB4;

// A bucket list: one word, the first node; a node is {next, overlay index}.
struct Node {
    Node* next;
    uint32_t value;
};

// 0x00736250, ret 8 (this unused): the nodes after `link` up to `last` freed,
// `link` then pointing at `last`; `last`.
Node* __fastcall CutAfter(void* list, void*, Node** link, Node* last);
// 0x00736440, ret 0x10 (this unused): copies of the nodes from `first` up to
// `last` put after `link`, in order.
void __fastcall CopyNodes(void* list, void*, Node** link, const Node* first, const Node* last, const void* tag);
// 0x00737DB0, ret 4: a list assigned from another (its nodes reused); this.
Node** __fastcall AssignList(Node** list, void*, Node* const* from);
// 0x00737E10, cdecl: overlay records destroyed over a range.
void __cdecl DestroyOverlays(char* first, char* last, const void* tag);
// 0x00738590, no arguments: a list emptied.
void __fastcall ClearList(Node** list);
// 0x007387D0, cdecl: lists assigned from one.
void __cdecl FillLists(Node** first, Node** last, Node* const* from);
// 0x00738800, cdecl: lists assigned from others, forwards; the end.
Node** __cdecl CopyLists(Node** first, Node** last, Node** to);
// 0x00738840, cdecl: n lists made as copies of one in new memory (none at a
// null place); the end.
Node** __cdecl FillNLists(Node** to, uint32_t count, Node* const* from);
// 0x00738880, cdecl: lists assigned from others, backwards; the first.
Node** __cdecl CopyListsBackward(Node** first, Node** last, Node** toEnd);
// 0x00739450, cdecl: lists copied into new memory (none at a null place);
// the end.
Node** __cdecl CopyNewLists(Node** first, Node** last, Node** to);
// 0x00739490, ret 4 (this = the manager + 4): the view interface's draw.
bool __fastcall ViewDraw(char* view, void*, void* context);
// 0x0073A170, cdecl: lists emptied over a range.
void __cdecl DestroyLists(Node** first, Node** last, const void* tag);
// 0x0073AC90, no arguments: every overlay and handle removed.
void __fastcall Clear(char* self);
// 0x0073AE60, ret 4 (this = the vector): the render states' count set.
void __fastcall ResizeStates(char* vector, void*, uint32_t count);
// 0x0073BA10, no arguments: the five render states.
void __fastcall SetupStates(char* self);
// 0x0073BB20, ret 0x14 (this = the buckets' vector): a new block for an
// insert of n copies of `from` at `at`.
void __fastcall GrowBuckets(char* vector, void*, Node** at, Node* const* from, const void* tag, uint32_t count, uint32_t atEnd);
// 0x0073BC20, no arguments: the constructor; this.
char* __fastcall Construct(char* self);
// 0x0073BE00, no arguments: the destructor's body.
void __fastcall Destruct(char* self);
// 0x0073C840, ret 0xC (this = the buckets' vector): n copies of a list
// inserted at `at`.
void __fastcall InsertLists(char* vector, void*, Node** at, uint32_t count, Node* const* from);
// 0x0073C980, ret 4: the deleting destructor; this.
char* __fastcall DeletingDestruct(char* self, void*, uint32_t flags);
// 0x0073CAB0, ret 8 (this = the buckets' vector): the count set, new lists
// copies of `from`.
void __fastcall ResizeBuckets(char* vector, void*, uint32_t count, Node* const* from);
// 0x0073CB10, no arguments: one bucket a cell, all empty.
void __fastcall SetupBuckets(char* self);
// 0x0073CB60, ret 0xC: Init (the terrain view, whether the manager draws
// the water's rows, the byte at +0x16).
void __fastcall Init(char* self, void*, void* view, uint32_t waterRows, uint32_t waterVertices);

} // namespace opensc4::overlay_manage::part3
