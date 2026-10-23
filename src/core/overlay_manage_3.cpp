// Our overlay manager's bookkeeping, part 3 (cSC4OverlayManager, build 641;
// docs/renderer.md, "Bookkeeping"): the set-up and the containers. The
// constructor, Init (the terrain view's 0x0075C420 calls it for each of its
// four managers), the render states and the bucket grid it sets up, Clear
// (every overlay and handle removed; the terrain view's 0x007560F0), the
// destructor, the view interface's draw, and the containers under them: the
// buckets' vector of list heads (resized, n lists inserted, a new block) and
// STLport's slist helpers for the lists in it (a list assigned, cut, its
// nodes copied, ranges of lists made, assigned forwards and backwards,
// emptied), and the overlay records destroyed over a range.
//
// The layout is in overlay_manage_3.h; part 1 (overlay_manage_1.cpp) has the
// getters, setters and removing, part 2 (overlay_manage_2.cpp) the adding.
// The detours and compare mode are in overlay_manage_3_hooks.cpp.
//
// Calls the code makes: part 2's range assigns 0x00735CB0 and 0x00735D10
// (Clear, by address, so they reach ours or the game's as part 2 is set);
// Draw 0x00738A50 by address; the render state's set-up 0x007D2B50 and the
// 8-byte vector's erase 0x00710B40 and insert 0x00638E80; cRZBaseUnknown's
// constructor 0x0090DA1E and destructor 0x0090D990; the texture's Release
// 0x008090E0; RZGetFramework and its slot 0x14 (the texture manager), the
// reference's release 0x004495C0; the texture manager's slot 0x14 (a texture
// by ID); the terrain's (0x00B43CF4) slots 0x18, 0x1C and 0x20; the message
// server's (0x00B43CCC) slots 0x14 and 0x18; the allocator 0x0090CF54, its
// free 0x0090CF63 and operator delete 0x005E5620. Calls among the 23 are
// ours directly.
#include "core/overlay_manage_3.h"

#include "core/game_access.h"

#include <opensc4/addresses.h>

#include <initializer_list>

namespace opensc4::overlay_manage::part3 {
namespace {

constexpr uint32_t kMessageOptions = 0xC9F775BB;
constexpr uint32_t kShadowTextureId = 0xAA592B77;
constexpr uint32_t kTextureManagerClass = 0x1AC0E11A, kTextureManagerIid = 0xFAC0E219;
constexpr uint32_t kMinusOneBits = 0xBF800000, kHalfBits = 0x3F000000;
constexpr uint32_t kNoneWord = 0xFFFFFFFF;
constexpr double kOneDouble = 1.0; // 0x00A81228, a float 1

template <typename R, typename... Args>
R This(std::uintptr_t function, void* self, Args... args)
{
    return reinterpret_cast<R(__thiscall*)(void*, Args...)>(function)(self, args...);
}

template <typename R, typename... Args>
R Cdecl(std::uintptr_t function, Args... args)
{
    return reinterpret_cast<R(__cdecl*)(Args...)>(function)(args...);
}

Node* NewNode()
{
    return Cdecl<Node*>(addr::SC4_stl_allocate, std::size_t{8});
}

void Free(void* block)
{
    Cdecl<void>(addr::SC4_Free, block);
}

void TextureRelease(void* texture)
{
    This<uint32_t>(addr::SC4TextureBinding_Release, texture);
}

// Byte arithmetic on the vectors' pointers, as the game's SAR of the
// difference.
int32_t Words(const void* first, const void* last)
{
    return static_cast<int32_t>(reinterpret_cast<std::uintptr_t>(last) - reinterpret_cast<std::uintptr_t>(first)) >> 2;
}

// A reference held in a field set: the new one AddRef'd (slot 4) and the old
// one released (slot 8), nothing when they are the same.
void SetReference(char* self, std::size_t field, void* object)
{
    void* const old = At<void*>(self, field);
    if (object == old)
        return;
    if (object)
        Virtual<uint32_t>(object, 0x4);
    At<void*>(self, field) = object;
    if (old)
        Virtual<uint32_t>(old, 0x8);
}

// A held reference dropped: the field cleared first, then the release.
void DropReference(char* self, std::size_t field)
{
    void* const object = At<void*>(self, field);
    if (!object)
        return;
    At<void*>(self, field) = nullptr;
    Virtual<uint32_t>(object, 0x8);
}

void ReleaseReference(char* self, std::size_t field)
{
    if (void* const object = At<void*>(self, field))
        Virtual<uint32_t>(object, 0x8);
}

} // namespace

// ====================================================================================
// The lists
// ====================================================================================

// 0x00736250: STLport's slist erase_after.
Node* __fastcall CutAfter(void*, void*, Node** link, Node* last)
{
    for (Node* node = *link; node != last;) {
        Node* const next = node->next;
        Free(node);
        node = next;
    }
    *link = last;
    return last;
}

// 0x00736440: STLport's slist insert_after_range. The value is written only
// where the node's second word is not at address 0 (the placement new's
// test).
void __fastcall CopyNodes(void*, void*, Node** link, const Node* first, const Node* last, const void*)
{
    Node** at = link;
    for (const Node* from = first; from != last; from = from->next) {
        Node* const node = NewNode();
        if (reinterpret_cast<std::uintptr_t>(node) + 4 != 0)
            node->value = from->value;
        node->next = nullptr;
        node->next = *at;
        *at = node;
        at = &node->next;
    }
}

// 0x00737DB0: the list's nodes take the other's values in order; what is
// left over is cut, what is missing copied after the last.
Node** __fastcall AssignList(Node** list, void*, Node* const* from)
{
    if (from == list)
        return list;
    Node* to = *list;
    const Node* value = *from;
    Node** link = list;
    for (; to; to = to->next) {
        if (!value) {
            CutAfter(list, nullptr, link, nullptr);
            return list;
        }
        to->value = value->value;
        value = value->next;
        link = &to->next;
    }
    if (value)
        CopyNodes(list, nullptr, link, value, nullptr, &from);
    else
        CutAfter(list, nullptr, link, nullptr);
    return list;
}

// 0x00738590.
void __fastcall ClearList(Node** list)
{
    for (Node* node = *list; node;) {
        Node* const next = node->next;
        Free(node);
        node = next;
    }
    *list = nullptr;
}

// 0x007387D0: STLport's fill.
void __cdecl FillLists(Node** first, Node** last, Node* const* from)
{
    for (Node** list = first; list != last; ++list)
        AssignList(list, nullptr, from);
}

// 0x00738800: STLport's copy, by count.
Node** __cdecl CopyLists(Node** first, Node** last, Node** to)
{
    int32_t count = Words(first, last);
    if (count <= 0)
        return to;
    for (; count; --count) {
        AssignList(to, nullptr, first);
        ++first;
        ++to;
    }
    return to;
}

// 0x00738840: STLport's uninitialized_fill_n.
Node** __cdecl FillNLists(Node** to, uint32_t count, Node* const* from)
{
    for (; count; --count, ++to)
        if (to) {
            *to = nullptr;
            CopyNodes(to, nullptr, to, *from, nullptr, &to);
        }
    return to;
}

// 0x00738880: STLport's copy_backward, by count.
Node** __cdecl CopyListsBackward(Node** first, Node** last, Node** toEnd)
{
    int32_t count = Words(first, last);
    if (count <= 0)
        return toEnd;
    for (; count; --count) {
        --last;
        --toEnd;
        AssignList(toEnd, nullptr, last);
    }
    return toEnd;
}

// 0x00739450: STLport's uninitialized_copy.
Node** __cdecl CopyNewLists(Node** first, Node** last, Node** to)
{
    for (; first != last; ++first, ++to)
        if (to) {
            *to = nullptr;
            CopyNodes(to, nullptr, to, *first, nullptr, &to);
        }
    return to;
}

// 0x0073A170: STLport's _Destroy over a range of lists.
void __cdecl DestroyLists(Node** first, Node** last, const void*)
{
    for (Node** list = first; list != last; ++list) {
        for (Node* node = *list; node;) {
            Node* const next = node->next;
            Free(node);
            node = next;
        }
        *list = nullptr;
    }
}

// ====================================================================================
// The buckets' vector
// ====================================================================================

// 0x0073BB20: a block for the old lists and the new ones, as many more as
// there are or the count when larger; the old lists copied around the new
// ones, then destroyed and their block freed.
void __fastcall GrowBuckets(char* vector, void*, Node** at, Node* const* from, const void*, uint32_t count, uint32_t atEnd)
{
    const uint32_t size = static_cast<uint32_t>(Words(At<Node**>(vector, 0), At<Node**>(vector, kEnd)));
    const uint32_t capacity = size + (size < count ? count : size);
    Node** const block = capacity ? Cdecl<Node**>(addr::SC4_stl_allocate, static_cast<std::size_t>(capacity * 4)) : nullptr;
    Node** end = CopyNewLists(At<Node**>(vector, 0), at, block);
    if (count == 1) {
        if (end) {
            *end = nullptr;
            CopyNodes(end, nullptr, end, *from, nullptr, &atEnd);
        }
        ++end;
    } else {
        end = FillNLists(end, count, from);
    }
    if (!static_cast<uint8_t>(atEnd))
        end = CopyNewLists(at, At<Node**>(vector, kEnd), end);
    DestroyLists(At<Node**>(vector, 0), At<Node**>(vector, kEnd), &atEnd);
    if (Node** const old = At<Node**>(vector, 0))
        Free(old);
    At<Node**>(vector, 0) = block;
    At<Node**>(vector, kEnd) = end;
    At<std::uintptr_t>(vector, kCapacity) = reinterpret_cast<std::uintptr_t>(block) + capacity * 4;
}

// 0x0073C840: STLport's _M_fill_insert. The value is copied into a local list
// first (it may be one of the vector's) unless the vector needs a new block,
// which takes it as it is.
void __fastcall InsertLists(char* vector, void*, Node** at, uint32_t count, Node* const* from)
{
    if (!count)
        return;
    if (static_cast<uint32_t>(Words(At<Node**>(vector, kEnd), At<Node**>(vector, kCapacity))) < count) {
        GrowBuckets(vector, nullptr, at, from, &count, count, 0);
        return;
    }
    Node* copy = nullptr;
    CopyNodes(&copy, nullptr, &copy, *from, nullptr, &count);
    Node** const end = At<Node**>(vector, kEnd);
    const uint32_t after = static_cast<uint32_t>(Words(at, end));
    if (after > count) {
        Node** const tail = end - count;
        CopyNewLists(tail, end, end);
        At<Node**>(vector, kEnd) = At<Node**>(vector, kEnd) + count;
        CopyListsBackward(at, tail, end);
        FillLists(at, at + count, &copy);
    } else {
        FillNLists(end, count - after, &copy);
        At<Node**>(vector, kEnd) = At<Node**>(vector, kEnd) + (count - after);
        CopyNewLists(at, end, At<Node**>(vector, kEnd));
        At<Node**>(vector, kEnd) = At<Node**>(vector, kEnd) + after;
        FillLists(at, end, &copy);
    }
    ClearList(&copy);
}

// 0x0073CAB0: STLport's resize: the lists past the count assigned onto
// themselves (an empty copy) and destroyed, or the missing ones inserted at
// the end.
void __fastcall ResizeBuckets(char* vector, void*, uint32_t count, Node* const* from)
{
    Node** const end = At<Node**>(vector, kEnd);
    Node** const begin = At<Node**>(vector, 0);
    const uint32_t size = static_cast<uint32_t>(Words(begin, end));
    if (count < size) {
        Node** const newEnd = CopyLists(end, end, begin + count);
        DestroyLists(newEnd, At<Node**>(vector, kEnd), &count);
        At<Node**>(vector, kEnd) = newEnd;
        return;
    }
    InsertLists(vector, nullptr, end, count - size, from);
}

// ====================================================================================
// The overlay records
// ====================================================================================

// 0x00737E10: each record's texture source and texture released (the fields
// are left as they were).
void __cdecl DestroyOverlays(char* first, char* last, const void*)
{
    for (char* record = first; record != last; record += kOverlaySize) {
        if (void* const source = At<void*>(record, 0xB0))
            Virtual<uint32_t>(source, 0x8);
        if (void* const texture = At<void*>(record, 0xAC))
            TextureRelease(texture);
    }
}

// ====================================================================================
// The manager
// ====================================================================================

// 0x00739490: the draw of the view interface at +4.
bool __fastcall ViewDraw(char* view, void*, void* context)
{
    This<void>(addr::cSC4OverlayManager_Draw, view - 4, context);
    return true;
}

// 0x0073AC90: the options message no longer wanted; the view, the terrain and
// the texture manager released; the overlay records assigned from an empty
// range (part 2's) and destroyed, the handles likewise, both free lists
// emptied; +0x14 cleared. The groups and the buckets are left as they are.
void __fastcall Clear(char* self)
{
    if (void* const server = Global<void*>(addr::SC4_MessageServer))
        Virtual<uint8_t>(server, 0x18, self ? self + kTarget : nullptr, kMessageOptions);
    DropReference(self, kView);
    DropReference(self, kTerrain);
    DropReference(self, kTextures);
    char tag = 0;
    char* const overlaysEnd = At<char*>(self, kOverlays + kEnd);
    char* const kept = Cdecl<char*>(addr::cSC4OverlayManager_AssignOverlays, overlaysEnd, overlaysEnd, At<char*>(self, kOverlays),
                                    static_cast<void*>(&tag), 0u);
    DestroyOverlays(kept, At<char*>(self, kOverlays + kEnd), &tag);
    At<char*>(self, kOverlays + kEnd) = kept;
    At<uint32_t>(self, kOverlays + kFirstFree) = kNoneWord;
    At<uint32_t>(self, kOverlays + kLastFree) = kNoneWord;
    char* const handlesEnd = At<char*>(self, kHandles + kEnd);
    char* const handles = Cdecl<char*>(addr::cSC4OverlayManager_AssignHandles, handlesEnd, handlesEnd, At<char*>(self, kHandles),
                                       static_cast<void*>(&tag), 0u);
    At<uint32_t>(self, kHandles + kFirstFree) = kNoneWord;
    At<uint32_t>(self, kHandles + kLastFree) = kNoneWord;
    At<char*>(self, kHandles + kEnd) = handles;
    At<uint8_t>(self, kStarted) = 0;
}

// 0x0073AE60: the vector of 8-byte render states made `count` long, new
// ones zero.
void __fastcall ResizeStates(char* vector, void*, uint32_t count)
{
    const uint32_t zero[2] = {0, 0};
    char* const begin = At<char*>(vector, 0);
    char* const end = At<char*>(vector, kEnd);
    const uint32_t size = static_cast<uint32_t>(static_cast<int32_t>(end - begin) >> 3);
    if (count < size)
        This<void>(addr::SC4Vector2Vector_EraseRange, vector, begin + count * 8, end);
    else
        This<void>(addr::SC4Vector2Vector_InsertN, vector, end, count - size, zero);
}

// 0x0073BA10: five render states (0x007D2B50, fourteen values each) and the
// bytes of the three state blocks at +0x38, +0x44 and +0x50 that differ
// from the constructor's.
void __fastcall SetupStates(char* self)
{
    char* const vector = self + kStates;
    ResizeStates(vector, nullptr, 5);
    const auto state = [&](std::size_t at, uint32_t a8, uint32_t a9, uint32_t a10) {
        This<void>(addr::SC4RenderState_Init, At<char*>(vector, 0) + at, 1u, 0u, 1u, 0u, 1u, 1u, 0u, a8, a9, a10, kNoneWord,
                   kMinusOneBits, 1u, kNoneWord);
    };
    state(0x0, 3, 4, 5);
    state(0x8, 3, 4, 1);
    state(0x10, 3, 0, 2);
    state(0x18, 4, 4, 5);
    This<void>(addr::SC4RenderState_Init, At<char*>(vector, 0) + 0x20, 1u, 1u, 1u, 1u, 1u, 1u, 0u, 1u, 4u, 5u, 4u, kHalfBits,
               kNoneWord, kNoneWord);
    for (std::size_t at : {0x38, 0x3D, 0x44, 0x49, 0x50, 0x51, 0x54, 0x55})
        At<uint8_t>(self, at) = 0;
    At<uint8_t>(self, 0x39) = 2;
    At<uint8_t>(self, 0x3C) = 1;
    At<uint8_t>(self, 0x3E) = 2;
    At<uint8_t>(self, 0x45) = 1;
    At<uint8_t>(self, 0x48) = 1;
    At<uint8_t>(self, 0x4A) = 1;
}

// 0x0073BC20: the vtables (the bases' first, cRZBaseUnknown made at +8), the
// shadow stages 2, the three state blocks, the extent unset
// (0x00B4B3C8), every vector empty and every free list -1. The cells, the
// clip, the bucket grid's fields and the bytes at +0x15 to +0x17 are left as
// the memory had them.
char* __fastcall Construct(char* self)
{
    At<uint32_t>(self, 0x0) = 0x00AB3930;
    At<uint32_t>(self, 0x4) = 0x00A881C0;
    This<void*>(addr::cRZBaseUnknown_Construct, self + kBase);
    At<uint32_t>(self, 0x4) = 0x00A881D4;
    At<uint32_t>(self, kBase) = 0x00A881E8;
    At<uint32_t>(self, kTarget) = 0x00A81174;
    At<uint32_t>(self, 0x0) = 0x00AB39E8;
    At<uint32_t>(self, 0x4) = 0x00AB39D0;
    At<uint32_t>(self, kBase) = 0x00AB39B8;
    At<uint32_t>(self, kTarget) = 0x00AB39A8;
    At<int32_t>(self, kShadowStages) = 2;
    At<uint8_t>(self, kStarted) = 0;
    At<uint32_t>(self, kDetail) = 0;
    At<uint8_t>(self, kInView) = 1;
    for (std::size_t at = kStates; at <= 0x34; at += 4)
        At<uint32_t>(self, at) = 0;
    // Three blocks of twelve bytes: 1 at +0 and +3 and +4 and +7, 0 at +1,
    // +5, +8, +9, +0xA and +0xB, 3 at +2 and +6.
    for (std::size_t block : {0x38, 0x44, 0x50}) {
        static constexpr uint8_t kBytes[12] = {1, 0, 3, 1, 1, 0, 3, 1, 0, 0, 0, 0};
        for (std::size_t i = 0; i < 12; ++i)
            At<uint8_t>(self, block + i) = kBytes[i];
    }
    for (std::size_t at : {kExtent, kExtent + 4, kExtent + 8})
        At<uint32_t>(self, at) = Global<uint32_t>(addr::STE_UnsetMatrixFloat);
    for (std::size_t at : {kTerrain, kView, kTextures})
        At<uint32_t>(self, at) = 0;
    for (std::size_t vector : {kOverlays, kHandles, kGroups}) {
        At<uint32_t>(self, vector) = 0;
        At<uint32_t>(self, vector + kEnd) = 0;
        At<uint32_t>(self, vector + kCapacity) = 0;
        At<uint32_t>(self, vector + kFirstFree) = kNoneWord;
        At<uint32_t>(self, vector + kLastFree) = kNoneWord;
    }
    At<uint32_t>(self, kShadowTexture) = 0;
    At<uint32_t>(self, kBuckets) = 0;
    At<uint32_t>(self, kBuckets + kEnd) = 0;
    At<uint32_t>(self, kBuckets + kCapacity) = 0;
    return self;
}

// 0x0073BE00: the vtables; the buckets destroyed and freed, the shadow
// texture released, the groups' and handles' blocks freed, the overlay
// records destroyed and freed, the texture manager, the view and the terrain
// released, the render states' block freed; cRZBaseUnknown's destructor.
// Nothing is cleared.
void __fastcall Destruct(char* self)
{
    At<uint32_t>(self, 0x0) = 0x00AB39E8;
    At<uint32_t>(self, 0x4) = 0x00AB39D0;
    At<uint32_t>(self, kBase) = 0x00AB39B8;
    At<uint32_t>(self, kTarget) = 0x00AB39A8;
    char tag = 0;
    DestroyLists(At<Node**>(self, kBuckets), At<Node**>(self, kBuckets + kEnd), &tag);
    if (void* const block = At<void*>(self, kBuckets))
        Free(block);
    if (void* const texture = At<void*>(self, kShadowTexture))
        TextureRelease(texture);
    if (void* const block = At<void*>(self, kGroups))
        Free(block);
    if (void* const block = At<void*>(self, kHandles))
        Free(block);
    DestroyOverlays(At<char*>(self, kOverlays), At<char*>(self, kOverlays + kEnd), &tag);
    if (void* const block = At<void*>(self, kOverlays))
        Free(block);
    ReleaseReference(self, kTextures);
    ReleaseReference(self, kView);
    ReleaseReference(self, kTerrain);
    if (void* const block = At<void*>(self, kStates))
        Free(block);
    char* const view = self + 4;
    This<void>(addr::cRZBaseUnknown_Destruct, view ? view + 4 : nullptr);
}

// 0x0073C980.
char* __fastcall DeletingDestruct(char* self, void*, uint32_t flags)
{
    Destruct(self);
    if (flags & 1)
        Cdecl<void>(addr::SC4_operator_delete, static_cast<void*>(self));
    return self;
}

// 0x0073CB10: a bucket a cell (+0xE4 0), as many across as the terrain has
// cells, every list empty; the stamp 0.
void __fastcall SetupBuckets(char* self)
{
    const int32_t across = At<int32_t>(self, kCellsAcross);
    const int32_t down = At<int32_t>(self, kCellsDown);
    At<int32_t>(self, kBucketsDown) = down;
    At<int32_t>(self, kBucketsAcross) = across;
    At<int32_t>(self, kBucketShift) = 0;
    Node* empty = nullptr;
    ResizeBuckets(self + kBuckets, nullptr, static_cast<uint32_t>(down * across), &empty);
    At<uint16_t>(self, kStamp) = 0;
}

// 0x0073CB60: the view kept; the bytes +0x15 and +0x16 given, +0x14 set;
// the texture manager from the framework, the terrain from 0x00B43CF4; the
// terrain's cells, a cell's size and its inverse, the terrain's size (the
// x87's products of the cells stored as floats and the size), the clip;
// the shadow texture by ID; the render states; the buckets unless the
// manager draws the water's rows; the options message wanted; the shadow
// stages and the detail from the driver (0x00B43CC4's +0x18).
void __fastcall Init(char* self, void*, void* view, uint32_t waterRows, uint32_t waterVertices)
{
    SetReference(self, kView, view);
    At<uint8_t>(self, kWaterRows) = static_cast<uint8_t>(waterRows);
    At<uint8_t>(self, kWaterVertices) = static_cast<uint8_t>(waterVertices);
    At<uint8_t>(self, kStarted) = 1;
    void* textures = nullptr;
    if (void* const framework = Cdecl<void*>(addr::RZGetFramework))
        Virtual<uint8_t>(framework, 0x14, kTextureManagerClass, kTextureManagerIid, static_cast<void*>(&textures));
    SetReference(self, kTextures, textures);
    This<void>(addr::SC4RefPtr_Destruct, static_cast<void*>(&textures));
    SetReference(self, kTerrain, Global<void*>(addr::SC4_Terrain));
    At<int32_t>(self, kCellsAcross) = Virtual<int32_t>(At<void*>(self, kTerrain), 0x1C);
    At<int32_t>(self, kCellsDown) = Virtual<int32_t>(At<void*>(self, kTerrain), 0x20);
    // The size comes back on the x87's stack and stays there for the
    // division and the products.
    const double size = Virtual<double>(At<void*>(self, kTerrain), 0x18);
    At<float>(self, kCellSize) = static_cast<float>(size);
    At<float>(self, kCellScale) = static_cast<float>(kOneDouble / size);
    const float across = static_cast<float>(At<int32_t>(self, kCellsAcross));
    const float down = static_cast<float>(At<int32_t>(self, kCellsDown));
    At<float>(self, kExtent + 8) = static_cast<float>(static_cast<double>(down) * size);
    At<float>(self, kExtent) = static_cast<float>(static_cast<double>(across) * size);
    At<float>(self, kExtent + 4) = static_cast<float>(static_cast<double>(across) * size);
    int32_t* const clip = &At<int32_t>(self, kClip);
    clip[0] = 0;
    clip[1] = 0;
    clip[2] = At<int32_t>(self, kCellsAcross);
    clip[3] = At<int32_t>(self, kCellsDown);
    void* const manager = At<void*>(self, kTextures);
    At<uint32_t>(self, 0xD8) = 0;
    At<uint32_t>(self, 0xDC) = 0;
    At<uint32_t>(self, 0x94) = 0;
    if (void* const texture = At<void*>(self, kShadowTexture)) {
        At<void*>(self, kShadowTexture) = nullptr;
        TextureRelease(texture);
    }
    Virtual<void>(manager, 0x14, kShadowTextureId, static_cast<void*>(&At<void*>(self, kShadowTexture)));
    SetupStates(self);
    if (!At<uint8_t>(self, kWaterRows))
        SetupBuckets(self);
    if (void* const server = Global<void*>(addr::SC4_MessageServer))
        Virtual<uint8_t>(server, 0x14, static_cast<void*>(self + kTarget), kMessageOptions);
    char* const driver = At<char*>(Global<char*>(addr::SC4_App43CC4), 0x18);
    At<int32_t>(self, kShadowStages) = At<int32_t>(driver, 0x1FC);
    At<uint32_t>(self, kDetail) = At<uint32_t>(At<char*>(Global<char*>(addr::SC4_App43CC4), 0x18), 0xBC);
}

} // namespace opensc4::overlay_manage::part3
