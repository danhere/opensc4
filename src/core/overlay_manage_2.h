// Our overlay manager's bookkeeping, part 2: the adding (src/core/overlay_manage_2.cpp
// for ours, overlay_manage_2_hooks.cpp for the detours and compare mode;
// docs/renderer.md, "Bookkeeping").
#pragma once
#include <cstddef>
#include <cstdint>

namespace opensc4::overlay_manage::part2 {

// The manager.
constexpr std::size_t kSize = 0xFC;
constexpr std::size_t kWaterRows = 0x15, kWaterVertices = 0x16;
constexpr std::size_t kShadowStages = 0x18;   // int32: no shadows when 0
constexpr std::size_t kCellsAcross = 0x5C, kCellsDown = 0x60;
constexpr std::size_t kCellScale = 0x68;      // float: 1 / a cell's size
constexpr std::size_t kClip = 0x78;           // int32[4]: the terrain's cells
constexpr std::size_t kTerrain = 0x88;
constexpr std::size_t kTextures = 0x90;       // the texture manager (0x1AC0E11A)
constexpr std::size_t kOverlays = 0x98, kHandles = 0xAC, kGroups = 0xC0, kBuckets = 0xF0;
// A vector of records: begin, end, capacity, then the first and last free
// record (-1 for none).
constexpr std::size_t kEnd = 0x4, kCapacity = 0x8, kFirstFree = 0xC, kLastFree = 0x10;

// The records.
constexpr std::size_t kOverlaySize = 0xB4, kHandleSize = 0x30, kGroupSize = 0x24;
constexpr std::size_t kFlags = 0x4, kDrawMode = 0x8, kStamp = 0xA, kCells = 0xC, kMatrix = 0x1C, kSecondMatrix = 0x5C,
                      kAlpha = 0x9C, kColour = 0xA0, kTexture = 0xAC, kTextureSource = 0xB0;
constexpr std::size_t kDecalInfo = 0x4, kAspect = 0x28, kTarget = 0x2C; // a handle's
constexpr std::size_t kGroupIds = 0x4;
constexpr int kGroupCount = 8;

constexpr uint32_t kNone = 0xFFFFFFFF;
constexpr uint32_t kHandleBit = 0x80000000;
constexpr uint32_t kIndexMask = 0x7FFFFFFF;

// 0x00735350, cdecl: groups copied to new memory (none written at a null
// place); the end.
uint32_t* __cdecl CopyGroups(const uint32_t* first, const uint32_t* last, uint32_t* to);
// 0x007354E0, ret 0x14: the cells a shadow covers.
void __fastcall ShadowCells(char* self, void*, const float* light, uint32_t count, const void* points, const float* box,
                            int32_t* out);
// 0x00735820, ret 4: an overlay record assigned.
char* __fastcall AssignOverlay(char* to, void*, const char* from);
// 0x007359F0, ret 4: an overlay record made as a copy.
char* __fastcall CopyOverlay(char* to, void*, const char* from);
// 0x00735CB0, cdecl: overlay records assigned over a range; the end.
char* __cdecl AssignOverlays(const char* first, const char* last, char* to);
// 0x00735D10, cdecl: handles assigned over a range; the end.
char* __cdecl AssignHandles(const char* first, const char* last, char* to);
// 0x00735F40, no arguments: an overlay record made empty.
char* __fastcall ResetOverlay(char* record);
// 0x00736280, cdecl: a handle copied (none at a null place).
char* __cdecl CopyHandle(char* to, const char* from);
// 0x007362E0, cdecl: overlay records copied to new memory; the end.
char* __cdecl CopyOverlays(const char* first, const char* last, char* to);
// 0x00736320, cdecl: n overlay records made from one; the end.
char* __cdecl FillOverlays(char* to, uint32_t count, const char* from);
// 0x00736350, cdecl: handles copied to new memory; the end.
char* __cdecl CopyHandles(const char* first, const char* last, char* to);
// 0x007363D0, cdecl: n handles made from one; the end.
char* __cdecl FillHandles(char* to, uint32_t count, const char* from);
// 0x007385C0, 0x007386A0, 0x0073A070, ret 0x14 (this = the vector): the
// vector grown by an insert of n records at `at`.
void __fastcall GrowHandles(char* vector, void*, char* at, const char* from, const void* tag, uint32_t count, uint32_t atEnd);
void __fastcall GrowGroups(char* vector, void*, char* at, const char* from, const void* tag, uint32_t count, uint32_t atEnd);
void __fastcall GrowOverlays(char* vector, void*, char* at, const char* from, const void* tag, uint32_t count, uint32_t atEnd);
// 0x007394B0, ret 0xC: a ring decal's eight overlays given their cells.
void __fastcall BuildRing(char* self, void*, const uint32_t* group, const float* rect, uint32_t buckets);
// 0x00739EC0, 0x00739FC0, 0x0073AEC0, ret 4 (this = the vector): a new
// record from the free list or the vector's end; its index.
uint32_t __fastcall NewHandle(char* vector, void*, const char* from);
uint32_t __fastcall NewGroup(char* vector, void*, const char* from);
uint32_t __fastcall NewOverlay(char* vector, void*, const char* from);
// 0x0073A1B0, ret 8: BuildDecal.
void __fastcall BuildDecal(char* self, void*, char* handle, uint32_t buckets);
// 0x0073AF80, ret 0x14: a decal from a texture source or a texture.
uint32_t __fastcall AddDecalFrom(char* self, void*, void* source, void* texture, const float* position, float size, float turn);
// 0x0073B8A0, ret 0x10: an overlay with a texture, cells, a matrix, flags.
uint32_t __fastcall AddOverlay(char* self, void*, void* texture, const int32_t* cells, const float* matrix, uint32_t flags);

// The interface's slots 3 to 11.
uint32_t __fastcall AddDecalSource(char* self, void*, void* source, const float* position, float size, float turn); // 3
uint32_t __fastcall AddDecalTexture(char* self, void*, void* texture, const float* position, float size, float turn); // 4
uint32_t __fastcall AddDecalKey(char* self, void*, const uint32_t* key, const float* position, float size, float turn); // 5
uint32_t __fastcall AddDecalIid(char* self, void*, uint32_t iid, const float* position, float size, float turn); // 6
uint32_t __fastcall AddRingDecal(char* self, void*, uint32_t iid, const float* position, float size, float turn); // 7
uint32_t __fastcall AddHeightMap(char* self, void*, uint32_t iid); // 8
uint32_t __fastcall AddShadowDecalKey(char* self, void*, const uint32_t* key, const float* position, float size, float turn,
                                      uint32_t mirror); // 9
uint32_t __fastcall AddShadowDecalIid(char* self, void*, uint32_t iid, const float* position, float size, float turn,
                                      uint32_t mirror); // 10
uint32_t __fastcall AddShadow(char* self, void*, void* texture, int32_t count, const float* points3, const float* points2,
                              const float* box, const void* transform); // 11

} // namespace opensc4::overlay_manage::part2
