// Shared between the parts of our cSC4LotConfiguration (build 641): the
// class map, the vtable slot by slot, the fields of a configuration and of
// its objects, and the functions the two files share. A configuration says
// what a lot of some size, zone and wealth is made of; the lot
// configuration manager (lotconfig_sim.h) keeps about 1,900 of them in a
// static map, loaded from the exemplars of group 0xA8FBD372. Written in two
// files: lotcfg_sim.cpp (the slots, the object list's helpers, our own
// object) and lotcfg_load.cpp (loading from and saving to an exemplar).
//
// The class: no class ID (never made through GZCOM; `new` at 0x00483B8D in
// the lot editor, 0x0048992F and 0x0048BDAD in the manager), 0xC0 bytes,
// constructor 0x004797A0, destructor 0x00479C20, deleting destructor
// 0x00479F50. One vtable. Code 0x004777B0 to 0x0047A77B, between
// cSC4LandfillOccupant (to 0x00477762) and the lot editor's windows (from
// 0x0047A780; 0x004892B0 to 0x00489650 is the editor's too: window
// functions on +0xD8, +0x130, +0x160 of a cGZWin, nothing of this class).
// Generic std::vector<bool> code lies inside the range and stays the
// game's (other classes call it): 0x00477770, 0x00477790, 0x004778D0,
// 0x00477910, 0x00477950, 0x004779E0, 0x00477A40, 0x00477CB0, 0x00477E20,
// 0x00477E70, 0x004780D0 (erase), 0x00478140 (resize); also 0x00478890
// (push_back of a byte vector) and 0x00478AA0 (a string helper).
//
// QueryInterface (0x004777B0): IIDs 0x00000001 and 0x68EDBEBC
// (cISC4LotConfiguration) the object.
//
// --- cISC4LotConfiguration, vtable 0x00A8CEB8, 34 slots (argument counts
// --- from each slot's own `ret N`; gzcom-dll's order is right) ------------
//    0 QueryInterface 0x004777B0 (ret 8)   1 AddRef 0x005F47F0 (shared)
//    2 Release 0x00479CE0                   3 GetID 0x004777E0: +0x8
//    4 GetName(cIGZString&) 0x004777F0: a jump to the string's slot 13
//      (Copy) with the name at +0xC
//    5 BelongsToFamily(id) 0x00477A90       6 GetFamilyIDArray 0x00477810
//    7 GetSize(uint32*, uint32*) 0x00477820
//    8 GetMaximumSlope 0x00477840 (+0x30)   9 GetMinimumSlope 0x00477850
//      (+0x34)   10 GetMaximumSlopeBeforeFoundation 0x00477860 (+0x38)
//   11 GetGrowthStage 0x00477870 (+0x3C)
//   12 GetMinBuildingCapacity 0x004798E0 (+0x3E)
//   13 GetMaxBuildingCapacity 0x004798F0 (+0x40)
//   14 IsCompatibleWithWater 0x00477880 (+0x42)
//   15 IsCompatibleWithZoneType(type) 0x00477D00 (a bit of +0x44)
//   16 IsCompatibleWithWealthType(type) 0x00477D50 (a bit of +0x58)
//   17 IsCompatibleWithPurposeType(type) 0x00477DA0 (a bit of +0x6C)
//   18 IsCompatibleWithStyleType(type) 0x00477AC0 (a search of +0x80)
//   19 IsCompatibleWithMonopolyPieceUse 0x004FC900 (shared stub: true, ret 4)
//   20 IsDoConstruction 0x00479900 (+0x8D)
//   21 GetRequiredRoads(vector<bool>&) 0x00478200 (4 bits from +0x8E)
//   22 GetLotConfigurationObjectCount 0x00477AF0
//   23 GetLotConfigurationObjectArray 0x00477890 (+0x90)
//   24, 25, 26 the first object of kind 0 (building), 2 (texture), 3
//      (foundation): 0x00477DF0, 0x00477E00, 0x00477E10 through 0x00477B10
//   27 HasObjectsOfType(kind) 0x00477B60
//   28 GetRetainingWallIDArray 0x004778A0 (+0x9C)
//   29 GetFenceIDArray 0x004778B0 (+0xA8)
//   30 GetFenceSegmentPositionArray 0x004778C0 (+0xB4)
//   31 Load(key, detail byte) 0x00479F70 (ret 8)
//   32 Save(key, segment) 0x00479580 (ret 8)
//   33 the deleting destructor 0x00479F50
//
// Not virtual: Reset 0x00479A10, WriteToExemplar 0x00478AC0 (both only
// called inside the class), and the object list's functions, most of them
// also called by the manager, the lot editor and the code at 0x0060Dxxx to
// 0x00610xxx and 0x0049Axxx to 0x0049Cxxx: the object's constructor
// 0x00478360, copy constructor 0x004788D0 and assignment 0x00478950, the
// copies 0x004789C0 (into raw storage) and 0x004789F0 (by assignment), the
// vector's copy constructor 0x00479670, erase 0x004796F0, destructor
// 0x00479760, growing insert 0x00479910 and push_back 0x00479BE0, the
// objects read from (0x00479D10) and written to (0x004783A0) an exemplar's
// properties, and the objects moved inside a rectangle (0x00477BB0).
//
// Fields of a configuration (evidence: the constructor 0x004797A0 sets
// every one; the getters above; Load 0x00479F70 and WriteToExemplar
// 0x00478AC0 give the properties):
//   +0x00 vtable            +0x04 uint32 reference count
//   +0x08 uint32 ID (the exemplar's instance; Load)
//   +0x0C the name, a cRZBaseString (vtable 0x00A80810, begin, end,
//         capacity at +0x10/+0x14/+0x18, its reference count +0x1C);
//         property 0x00000020
//   +0x20, +0x21 uint8 width and depth in cells (property 0x88EDC790)
//   +0x24 vector<uint32> family IDs (0x88EDC791)
//   +0x30 float maximum slope (0xE99B068C; 90), +0x34 float minimum slope
//         (0x699B08A4; 0), +0x38 float maximum slope before a foundation
//         (0x88EDC792; 90)
//   +0x3C uint8 growth stage (0x27812837; 1)
//   +0x3E, +0x40 uint16 least and greatest building capacity (0xFFFF, 0;
//         the manager's message fills them)
//   +0x42 uint8 compatible with water (0x89136440)
//   +0x44 vector<bool> of 16 zone types (0x88EDC793), +0x58 of 4 wealth
//         types (0x88EDC795), +0x6C of 9 purpose types (0x88EDC796): each
//         {uint32* word, uint32 bit} for the first and for the end bit,
//         then the end of the storage (0x14 bytes)
//   +0x80 vector<uint32> style types (the manager's message fills it)
//   +0x8C uint8 written by the manager's message (a building's exemplar
//         lacks property 0x2781283F); nothing in the class reads it
//   +0x8D uint8 do construction (0xE99B068D)
//   +0x8E uint8 required roads, a bit per side (0x4A4A88F0; 8)
//   +0x90 vector of objects (0x34 bytes each; 0x88EDC900 and following)
//   +0x9C vector<uint32> retaining wall types (0x88EDC798)
//   +0xA8 vector<uint32> fence types, +0xB4 vector of 4 int8 fence segment
//         positions (both from 0x88EDC797)
//   +0x3D, +0x43, +0x8F padding, never written.
//
// A configuration object (cSC4LotConfigurationObject; evidence: the copy
// constructor 0x004788D0, LoadObjects 0x00479D10, WriteObjects 0x004783A0):
//   +0x00 uint8 kind (0 building, 1 prop, 2 texture, 3 foundation, 4 flora,
//         5 water, 6 land, 7 network)   +0x01 uint8 flags (0x30: the detail
//         level)   +0x02 uint8 orientation   +0x03 uint8 mirrored
//   +0x04, +0x08, +0x0C float x, y, z (the property's 16.16 fixed point)
//   +0x10, +0x14 float greatest x and z; +0x18, +0x1C float least x and z
//   +0x20 uint32 (a foundation's type; the property's 11th value)
//   +0x24 uint32 the object's ID (the 12th value, or a counter for
//         exemplars older than version 2)
//   +0x28 vector<uint32> type or family IDs (the values after the 12th)
#pragma once

#include "core/game_access.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>

namespace opensc4::lotcfg_sim {

inline constexpr uint32_t kIidUnknown = 0x00000001;
inline constexpr uint32_t kIidConfiguration = 0x68EDBEBC;
inline constexpr uint32_t kIidExemplar = 0xA52160F5;
inline constexpr uint32_t kIidResource = 0x456B8F1D;

inline constexpr std::size_t kSize = 0xC0;
inline constexpr std::size_t kSlots = 34;
inline constexpr std::size_t kObjectSize = 0x34;

namespace field {
inline constexpr std::size_t kRefCount = 0x04;
inline constexpr std::size_t kId = 0x08;
inline constexpr std::size_t kName = 0x0C;
inline constexpr std::size_t kWidth = 0x20;
inline constexpr std::size_t kDepth = 0x21;
inline constexpr std::size_t kFamilies = 0x24;
inline constexpr std::size_t kMaxSlope = 0x30;
inline constexpr std::size_t kMinSlope = 0x34;
inline constexpr std::size_t kFoundationSlope = 0x38;
inline constexpr std::size_t kGrowthStage = 0x3C;
inline constexpr std::size_t kMinCapacity = 0x3E;
inline constexpr std::size_t kMaxCapacity = 0x40;
inline constexpr std::size_t kWater = 0x42;
inline constexpr std::size_t kZoneTypes = 0x44;
inline constexpr std::size_t kWealthTypes = 0x58;
inline constexpr std::size_t kPurposeTypes = 0x6C;
inline constexpr std::size_t kStyles = 0x80;
inline constexpr std::size_t kUnflagged = 0x8C;
inline constexpr std::size_t kDoConstruction = 0x8D;
inline constexpr std::size_t kRoads = 0x8E;
inline constexpr std::size_t kObjects = 0x90;
inline constexpr std::size_t kRetainingWalls = 0x9C;
inline constexpr std::size_t kFences = 0xA8;
inline constexpr std::size_t kFencePositions = 0xB4;
} // namespace field

// An object's fields.
namespace object {
inline constexpr std::size_t kKind = 0x00;
inline constexpr std::size_t kFlags = 0x01;
inline constexpr std::size_t kOrientation = 0x02;
inline constexpr std::size_t kMirrored = 0x03;
inline constexpr std::size_t kX = 0x04;
inline constexpr std::size_t kY = 0x08;
inline constexpr std::size_t kZ = 0x0C;
inline constexpr std::size_t kMaxX = 0x10;
inline constexpr std::size_t kMaxZ = 0x14;
inline constexpr std::size_t kMinX = 0x18;
inline constexpr std::size_t kMinZ = 0x1C;
inline constexpr std::size_t kFoundation = 0x20;
inline constexpr std::size_t kId = 0x24;
inline constexpr std::size_t kIds = 0x28;
} // namespace object

struct Vector32 {
    uint32_t* begin;
    uint32_t* end;
    uint32_t* capacity;
};

struct ByteVector {
    uint8_t* begin;
    uint8_t* end;
    uint8_t* capacity;
};

// A vector of 0x34-byte configuration objects.
struct ObjectVector {
    char* begin;
    char* end;
    char* capacity;
};

// The game's std::vector<bool>: the first and the end bit as (word, bit),
// then the end of the storage.
struct BitVector {
    uint32_t* beginWord;
    uint32_t beginBit;
    uint32_t* endWord;
    uint32_t endBit;
    uint32_t* capacity;
};

// The game's cRZBaseString.
struct GameString {
    std::uintptr_t vtable;
    char* begin;
    char* end;
    char* capacity;
    uint32_t references;
};

// The number of bits (inline in the game's callers).
inline uint32_t BitCount(const BitVector* bits)
{
    return (static_cast<uint32_t>(bits->endWord - bits->beginWord) << 5) - bits->beginBit + bits->endBit;
}

// The word and the bit of an iterator moved by `index` (the game's inline
// operator+: a signed division by 32 rounding towards zero, a negative
// remainder taken from the word before).
inline uint32_t* BitWord(const BitVector* bits, uint32_t index, uint32_t& bit)
{
    const int32_t offset = static_cast<int32_t>(bits->beginBit + index);
    uint32_t* word = bits->beginWord + offset / 32;
    int32_t remainder = offset % 32;
    if (remainder < 0) {
        remainder += 32;
        --word;
    }
    bit = static_cast<uint32_t>(remainder);
    return word;
}

// The objects of a vector (the game divides by 0x34 with a signed multiply).
inline int32_t ObjectCount(const ObjectVector* objects)
{
    return static_cast<int32_t>(objects->end - objects->begin) / static_cast<int32_t>(kObjectSize);
}

inline void* Allocate(std::size_t size)
{
    return reinterpret_cast<void*(__cdecl*)(std::size_t)>(addr::SC4_stl_allocate)(size);
}
inline void Free(void* block) { reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(block); }

// Logs "lotcfg: first <name>" once per flag.
void FirstCall(const char* name, bool& logged);

struct Slot {
    int index;
    std::uintptr_t original;
    void* ours;
};

// Swaps the slots of 0x00A8CEB8 when every one still holds its original.
bool SwapSlots(const Slot* slots, std::size_t count, const char* what);

struct Detour {
    std::uintptr_t address;
    void* ours;
    void* original;
};

// Installs every detour of a list (entry hooks with the expected bytes).
bool InstallDetours(Detour* detours, std::size_t count, const char* what);

// --- lotcfg_sim.cpp: the object list (the game's addresses in the file) ----------
void* __fastcall CopyObject(char* self, void*, const char* other);
void* __fastcall InsertObjects(ObjectVector* self, void*, char* where, const char* value, void* tag, uint32_t count,
                               uint8_t atEnd);
char* __fastcall EraseObjects(ObjectVector* self, void*, char* first, char* last);
void __fastcall DestroyObjects(ObjectVector* self, void*);
ObjectVector* __fastcall CopyObjectVector(ObjectVector* self, void*, const ObjectVector* other);
uint8_t __cdecl ClampObjects(ObjectVector* objects, const float* rect);
// push_back as the game's inline code: in place when there is room, else
// through InsertObjects.
void PushBackObject(ObjectVector* objects, const char* value);
// 0x00479A10: a configuration back to its constructed state.
uint8_t Reset(char* self);

// --- lotcfg_load.cpp ---------------------------------------------------------------
uint8_t __fastcall Load(char* self, void*, const uint32_t* key, uint32_t detail);
uint8_t __fastcall Save(char* self, void*, const uint32_t* key, void* segment);

} // namespace opensc4::lotcfg_sim
