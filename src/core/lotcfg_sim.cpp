// Our cSC4LotConfiguration (build 641; the class map is in lotcfg_sim.h):
// the getters and queries, the list of configuration objects (copy, insert,
// erase, the functions the manager and other game code call directly) and
// our own object (constructor, destructor, identity). Loading from and
// saving to an exemplar are in lotcfg_load.cpp.
//
// Every virtual call below has its argument count from the callee's own
// `ret N`: the configuration's vtable 0x00A8CEB8 (slots 1, 33) and the
// name's cRZBaseString 0x00A80810 (slot 13, Copy: one argument; GetName
// jumps to it with the caller's string, which may be another cIGZString
// implementation: gzcom-dll's cIGZString has the same slot).
#include "core/lotcfg_sim.h"

#include "core/address_check.h"
#include "core/game.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace opensc4::lotcfg_sim {

void FirstCall(const char* name, bool& logged)
{
    if (logged)
        return;
    logged = true;
    log::Write("lotcfg: first %s", name);
}

bool SwapSlots(const Slot* slots, std::size_t count, const char* what)
{
    if (!game::IsBuild641())
        return false;
    const std::uintptr_t table = addr::cSC4LotConfiguration_vftable;
    for (std::size_t i = 0; i < count; ++i) {
        const Slot& slot = slots[i];
        const auto* const vtable = reinterpret_cast<const std::uintptr_t*>(table);
        if (vtable[slot.index] != slot.original) {
            log::Write("lotcfg %s: slot %d holds %08X, expected %08X; not installing", what, slot.index,
                       static_cast<unsigned>(vtable[slot.index]), static_cast<unsigned>(slot.original));
            return false;
        }
    }
    for (std::size_t i = 0; i < count; ++i) {
        const Slot& slot = slots[i];
        auto* const entry = reinterpret_cast<std::uintptr_t*>(table) + slot.index;
        DWORD old = 0;
        if (!VirtualProtect(entry, sizeof(*entry), PAGE_READWRITE, &old))
            return false;
        *entry = reinterpret_cast<std::uintptr_t>(slot.ours);
        VirtualProtect(entry, sizeof(*entry), old, &old);
    }
    log::Write("lotcfg %s: %u slots are ours", what, static_cast<unsigned>(count));
    return true;
}

bool InstallDetours(Detour* detours, std::size_t count, const char* what)
{
    if (!game::IsBuild641())
        return false;
    for (std::size_t i = 0; i < count; ++i) {
        Detour& detour = detours[i];
        const addr::Entry* const entry = addr_check::Find(detour.address);
        if (!entry || !hook::Install(*entry, detour.ours, &detour.original)) {
            log::Write("lotcfg %s: no detour at %08X", what, static_cast<unsigned>(detour.address));
            return false;
        }
    }
    log::Write("lotcfg %s: %u detours", what, static_cast<unsigned>(count));
    return true;
}

namespace {

using ResizeBitsFn = void(__thiscall*)(BitVector* bits, uint32_t count, uint32_t value);
using EraseBitsFn = void*(__thiscall*)(BitVector* bits, void* result, uint32_t* firstWord, uint32_t firstBit,
                                       uint32_t* lastWord, uint32_t lastBit);
using CopyVectorFn = void*(__thiscall*)(void* vector, const void* other);

const auto ResizeBits = reinterpret_cast<ResizeBitsFn>(addr::SC4BitVector_Resize);
const auto EraseBits = reinterpret_cast<EraseBitsFn>(addr::SC4BitVector_Erase);
const auto CopyConstructIds = reinterpret_cast<CopyVectorFn>(addr::SC4Uint32Vector_Copy);
const auto AssignIds = reinterpret_cast<CopyVectorFn>(addr::SC4Uint32Vector_CopyAssign);

ObjectVector* Objects(char* self) { return reinterpret_cast<ObjectVector*>(self + field::kObjects); }

// --- The slots ---------------------------------------------------------------------

// Slot 3 (0x004777E0).
uint32_t __fastcall GetID(char* self, void*)
{
    static bool logged = false;
    FirstCall("GetID", logged);
    return At<uint32_t>(self, field::kId);
}

// Slot 4 (0x004777F0): a jump to the given string's slot 13 with our name,
// so what that returns is returned.
uint32_t __fastcall GetName(char* self, void*, void* name)
{
    static bool logged = false;
    FirstCall("GetName", logged);
    return Virtual<uint32_t>(name, 0x34, static_cast<void*>(self + field::kName));
}

// Slot 5 (0x00477A90): the count is signed.
uint8_t __fastcall BelongsToFamily(char* self, void*, uint32_t family)
{
    static bool logged = false;
    FirstCall("BelongsToFamily", logged);
    const Vector32* const families = reinterpret_cast<const Vector32*>(self + field::kFamilies);
    const int32_t count = static_cast<int32_t>(families->end - families->begin);
    for (int32_t i = 0; i < count; ++i)
        if (families->begin[i] == family)
            return 1;
    return 0;
}

// Slot 6 (0x00477810).
void* __fastcall GetFamilyIDArray(char* self, void*)
{
    static bool logged = false;
    FirstCall("GetFamilyIDArray", logged);
    return self + field::kFamilies;
}

// Slot 7 (0x00477820). EAX is left as the depth; the header's bool is
// whatever its low byte is.
uint32_t __fastcall GetSize(char* self, void*, uint32_t* width, uint32_t* depth)
{
    static bool logged = false;
    FirstCall("GetSize", logged);
    *width = At<uint8_t>(self, field::kWidth);
    const uint32_t cells = At<uint8_t>(self, field::kDepth);
    *depth = cells;
    return cells;
}

// Slots 8, 9, 10 (0x00477840, 0x00477850, 0x00477860): floats in ST0.
float __fastcall GetMaximumSlope(char* self, void*)
{
    static bool logged = false;
    FirstCall("GetMaximumSlope", logged);
    return At<float>(self, field::kMaxSlope);
}

float __fastcall GetMinimumSlope(char* self, void*)
{
    static bool logged = false;
    FirstCall("GetMinimumSlope", logged);
    return At<float>(self, field::kMinSlope);
}

float __fastcall GetMaximumSlopeBeforeFoundation(char* self, void*)
{
    static bool logged = false;
    FirstCall("GetMaximumSlopeBeforeFoundation", logged);
    return At<float>(self, field::kFoundationSlope);
}

// Slot 11 (0x00477870).
uint8_t __fastcall GetGrowthStage(char* self, void*)
{
    static bool logged = false;
    FirstCall("GetGrowthStage", logged);
    return At<uint8_t>(self, field::kGrowthStage);
}

// Slots 12, 13 (0x004798E0, 0x004798F0): AX.
uint16_t __fastcall GetMinBuildingCapacity(char* self, void*)
{
    static bool logged = false;
    FirstCall("GetMinBuildingCapacity", logged);
    return At<uint16_t>(self, field::kMinCapacity);
}

uint16_t __fastcall GetMaxBuildingCapacity(char* self, void*)
{
    static bool logged = false;
    FirstCall("GetMaxBuildingCapacity", logged);
    return At<uint16_t>(self, field::kMaxCapacity);
}

// Slot 14 (0x00477880).
uint8_t __fastcall IsCompatibleWithWater(char* self, void*)
{
    static bool logged = false;
    FirstCall("IsCompatibleWithWater", logged);
    return At<uint8_t>(self, field::kWater);
}

// A bit of one of the three bit vectors, 0 or 1 in EAX. The index is not
// tested against the vector's size, as in the game.
uint32_t TestBit(char* self, std::size_t offset, uint32_t index)
{
    uint32_t bit = 0;
    const uint32_t* const word = BitWord(reinterpret_cast<const BitVector*>(self + offset), index, bit);
    return (*word & (1u << bit)) ? 1u : 0u;
}

// Slot 15 (0x00477D00).
uint32_t __fastcall IsCompatibleWithZoneType(char* self, void*, uint32_t type)
{
    static bool logged = false;
    FirstCall("IsCompatibleWithZoneType", logged);
    return TestBit(self, field::kZoneTypes, type);
}

// Slot 16 (0x00477D50).
uint32_t __fastcall IsCompatibleWithWealthType(char* self, void*, uint32_t type)
{
    static bool logged = false;
    FirstCall("IsCompatibleWithWealthType", logged);
    return TestBit(self, field::kWealthTypes, type);
}

// Slot 17 (0x00477DA0).
uint32_t __fastcall IsCompatibleWithPurposeType(char* self, void*, uint32_t type)
{
    static bool logged = false;
    FirstCall("IsCompatibleWithPurposeType", logged);
    return TestBit(self, field::kPurposeTypes, type);
}

// Slot 18 (0x00477AC0).
uint8_t __fastcall IsCompatibleWithStyleType(char* self, void*, uint32_t type)
{
    static bool logged = false;
    FirstCall("IsCompatibleWithStyleType", logged);
    const Vector32* const styles = reinterpret_cast<const Vector32*>(self + field::kStyles);
    for (const uint32_t* style = styles->begin; style != styles->end; ++style)
        if (*style == type)
            return 1;
    return 0;
}

// Slot 20 (0x00479900).
uint8_t __fastcall IsDoConstruction(char* self, void*)
{
    static bool logged = false;
    FirstCall("IsDoConstruction", logged);
    return At<uint8_t>(self, field::kDoConstruction);
}

// Slot 21 (0x00478200): the caller's vector made 4 bits long (new bits
// clear) and set from the road byte's low four bits. EAX is left as the
// last mask (inverted when the bit was cleared), as in the game.
uint32_t __fastcall GetRequiredRoads(char* self, void*, BitVector* roads)
{
    static bool logged = false;
    FirstCall("GetRequiredRoads", logged);
    if (BitCount(roads) != 4)
        ResizeBits(roads, 4, 0);
    uint32_t last = 0;
    for (uint32_t i = 0; i < 4; ++i) {
        uint32_t bit = 0;
        uint32_t* const word = BitWord(roads, i, bit);
        const uint32_t mask = 1u << bit;
        if (At<uint8_t>(self, field::kRoads) & (1u << i)) {
            *word |= mask;
            last = mask;
        } else {
            *word &= ~mask;
            last = ~mask;
        }
    }
    return last;
}

// Slot 22 (0x00477AF0).
int32_t __fastcall GetLotConfigurationObjectCount(char* self, void*)
{
    static bool logged = false;
    FirstCall("GetLotConfigurationObjectCount", logged);
    return ObjectCount(Objects(self));
}

// Slot 23 (0x00477890).
void* __fastcall GetLotConfigurationObjectArray(char* self, void*)
{
    static bool logged = false;
    FirstCall("GetLotConfigurationObjectArray", logged);
    return self + field::kObjects;
}

// 0x00477B10: the first object of a kind, or null.
char* FindObjectOfKind(char* self, uint32_t kind)
{
    const ObjectVector* const objects = Objects(self);
    const int32_t count = ObjectCount(objects);
    char* at = objects->begin;
    for (int32_t i = 0; i < count; ++i, at += kObjectSize)
        if (At<uint8_t>(at, object::kKind) == kind)
            return at;
    return nullptr;
}

// Slot 24 (0x00477DF0).
char* __fastcall GetPrimaryBuildingConfigurationObject(char* self, void*)
{
    static bool logged = false;
    FirstCall("GetPrimaryBuildingConfigurationObject", logged);
    return FindObjectOfKind(self, 0);
}

// Slot 25 (0x00477E00).
char* __fastcall GetPrimaryBaseConfigurationObject(char* self, void*)
{
    static bool logged = false;
    FirstCall("GetPrimaryBaseConfigurationObject", logged);
    return FindObjectOfKind(self, 2);
}

// Slot 26 (0x00477E10).
char* __fastcall GetPrimaryFoundationConfigurationObject(char* self, void*)
{
    static bool logged = false;
    FirstCall("GetPrimaryFoundationConfigurationObject", logged);
    return FindObjectOfKind(self, 3);
}

// Slot 27 (0x00477B60).
uint8_t __fastcall HasObjectsOfType(char* self, void*, uint32_t kind)
{
    static bool logged = false;
    FirstCall("HasObjectsOfType", logged);
    return FindObjectOfKind(self, kind) ? 1 : 0;
}

// Slots 28, 29, 30 (0x004778A0, 0x004778B0, 0x004778C0).
void* __fastcall GetRetainingWallIDArray(char* self, void*)
{
    static bool logged = false;
    FirstCall("GetRetainingWallIDArray", logged);
    return self + field::kRetainingWalls;
}

void* __fastcall GetFenceIDArray(char* self, void*)
{
    static bool logged = false;
    FirstCall("GetFenceIDArray", logged);
    return self + field::kFences;
}

void* __fastcall GetFenceSegmentPositionArray(char* self, void*)
{
    static bool logged = false;
    FirstCall("GetFenceSegmentPositionArray", logged);
    return self + field::kFencePositions;
}

#define OPENSC4_LOTCFG_SLOT(index, name) {index, addr::cSC4LotConfiguration_##name, reinterpret_cast<void*>(&name)}

const Slot kSlotSwaps[] = {
    OPENSC4_LOTCFG_SLOT(3, GetID),
    OPENSC4_LOTCFG_SLOT(4, GetName),
    OPENSC4_LOTCFG_SLOT(5, BelongsToFamily),
    OPENSC4_LOTCFG_SLOT(6, GetFamilyIDArray),
    OPENSC4_LOTCFG_SLOT(7, GetSize),
    OPENSC4_LOTCFG_SLOT(8, GetMaximumSlope),
    OPENSC4_LOTCFG_SLOT(9, GetMinimumSlope),
    OPENSC4_LOTCFG_SLOT(10, GetMaximumSlopeBeforeFoundation),
    OPENSC4_LOTCFG_SLOT(11, GetGrowthStage),
    OPENSC4_LOTCFG_SLOT(12, GetMinBuildingCapacity),
    OPENSC4_LOTCFG_SLOT(13, GetMaxBuildingCapacity),
    OPENSC4_LOTCFG_SLOT(14, IsCompatibleWithWater),
    OPENSC4_LOTCFG_SLOT(15, IsCompatibleWithZoneType),
    OPENSC4_LOTCFG_SLOT(16, IsCompatibleWithWealthType),
    OPENSC4_LOTCFG_SLOT(17, IsCompatibleWithPurposeType),
    OPENSC4_LOTCFG_SLOT(18, IsCompatibleWithStyleType),
    OPENSC4_LOTCFG_SLOT(20, IsDoConstruction),
    OPENSC4_LOTCFG_SLOT(21, GetRequiredRoads),
    OPENSC4_LOTCFG_SLOT(22, GetLotConfigurationObjectCount),
    OPENSC4_LOTCFG_SLOT(23, GetLotConfigurationObjectArray),
    OPENSC4_LOTCFG_SLOT(24, GetPrimaryBuildingConfigurationObject),
    OPENSC4_LOTCFG_SLOT(25, GetPrimaryBaseConfigurationObject),
    OPENSC4_LOTCFG_SLOT(26, GetPrimaryFoundationConfigurationObject),
    OPENSC4_LOTCFG_SLOT(27, HasObjectsOfType),
    OPENSC4_LOTCFG_SLOT(28, GetRetainingWallIDArray),
    OPENSC4_LOTCFG_SLOT(29, GetFenceIDArray),
    OPENSC4_LOTCFG_SLOT(30, GetFenceSegmentPositionArray),
};

bool InstallSlots(registry::Mode)
{
    return SwapSlots(kSlotSwaps, sizeof(kSlotSwaps) / sizeof(kSlotSwaps[0]), "slots");
}

} // namespace

// --- The object list ---------------------------------------------------------------

// 0x004788D0: the copy constructor of an object (the ID list copied by the
// game's vector copy constructor: storage for exactly its values).
void* __fastcall CopyObject(char* self, void*, const char* other)
{
    static bool logged = false;
    FirstCall("CopyObject", logged);
    std::memcpy(self, other, object::kIds);
    CopyConstructIds(self + object::kIds, other + object::kIds);
    return self;
}

namespace {

// 0x00478950: an object's assignment (the ID list by the game's vector
// assignment). Only the lot editor calls it.
void* __fastcall AssignObject(char* self, void*, const char* other)
{
    static bool logged = false;
    FirstCall("AssignObject", logged);
    std::memmove(self, other, object::kIds);
    AssignIds(self + object::kIds, other + object::kIds);
    return self;
}

// 0x00478360: an object's default constructor (only the lot editor calls
// it): the position from the float at 0x00B21670, an empty rectangle from 0
// to 1, no IDs. The kind, flags, +0x20 and +0x24 stay as they were.
void* __fastcall ConstructObject(char* self, void*)
{
    static bool logged = false;
    FirstCall("ConstructObject", logged);
    const uint32_t position = Global<uint32_t>(addr::cSC4LotConfiguration_ObjectDefaultPosition);
    At<uint32_t>(self, object::kX) = position;
    At<uint32_t>(self, object::kY) = position;
    At<uint32_t>(self, object::kZ) = position;
    At<float>(self, object::kMaxX) = 0.0f;
    At<float>(self, object::kMaxZ) = 0.0f;
    At<float>(self, object::kMinX) = 1.0f;
    At<float>(self, object::kMinZ) = 1.0f;
    Vector32* const ids = reinterpret_cast<Vector32*>(self + object::kIds);
    ids->begin = nullptr;
    ids->end = nullptr;
    ids->capacity = nullptr;
    return self;
}

// 0x004789C0: objects copied into raw storage; the end of the copies.
char* CopyObjectsInto(const char* first, const char* last, char* out)
{
    for (; first != last; first += kObjectSize, out += kObjectSize)
        if (out)
            CopyObject(out, nullptr, first);
    return out;
}

// 0x004789F0: objects copied by assignment; the end of the copies. The
// last two arguments are not read.
char* __cdecl CopyObjects(const char* first, const char* last, char* out, void*, uint32_t)
{
    static bool logged = false;
    FirstCall("CopyObjects", logged);
    int32_t count = static_cast<int32_t>(last - first) / static_cast<int32_t>(kObjectSize);
    if (count <= 0)
        return out;
    do {
        std::memmove(out, first, object::kIds);
        AssignIds(out + object::kIds, first + object::kIds);
        first += kObjectSize;
        out += kObjectSize;
    } while (--count != 0);
    return out;
}

} // namespace

// 0x00479760: every object's ID list freed, then the storage. The vector's
// pointers stay.
void __fastcall DestroyObjects(ObjectVector* self, void*)
{
    static bool logged = false;
    FirstCall("DestroyObjects", logged);
    for (char* at = self->begin; at != self->end; at += kObjectSize)
        if (void* const ids = At<void*>(at, object::kIds))
            Free(ids);
    if (self->begin)
        Free(self->begin);
}

// 0x00479670: the vector's copy constructor (storage for exactly the
// other's objects; none for an empty one).
ObjectVector* __fastcall CopyObjectVector(ObjectVector* self, void*, const ObjectVector* other)
{
    static bool logged = false;
    FirstCall("CopyObjectVector", logged);
    const int32_t count = ObjectCount(other);
    self->begin = nullptr;
    self->end = nullptr;
    self->capacity = nullptr;
    char* storage = nullptr;
    if (count != 0)
        storage = static_cast<char*>(Allocate(static_cast<std::size_t>(count * static_cast<int32_t>(kObjectSize))));
    self->begin = storage;
    self->end = storage;
    self->capacity = storage + count * static_cast<int32_t>(kObjectSize);
    self->end = CopyObjectsInto(other->begin, other->end, storage);
    return self;
}

// 0x004796F0: erase(first, last): the objects after `last` assigned down,
// the ID lists of what is left over freed. Returns `first`.
char* __fastcall EraseObjects(ObjectVector* self, void*, char* first, char* last)
{
    static bool logged = false;
    FirstCall("EraseObjects", logged);
    uint8_t tag = 0;
    char* const end = CopyObjects(last, self->end, first, &tag, 0);
    char* const old = self->end;
    for (char* at = end; at != old; at += kObjectSize)
        if (void* const ids = At<void*>(at, object::kIds))
            Free(ids);
    self->end = end;
    return first;
}

// 0x00479910: the growing insert of `count` copies of `value` at `where`
// (new storage for the size plus the greater of size and count; the objects
// before `where` copied, the copies made, the objects after `where` copied
// unless `atEnd`; the old objects destroyed and their storage freed). EAX
// is left as the new storage.
void* __fastcall InsertObjects(ObjectVector* self, void*, char* where, const char* value, void*, uint32_t count,
                               uint8_t atEnd)
{
    static bool logged = false;
    FirstCall("InsertObjects", logged);
    const uint32_t size = static_cast<uint32_t>(ObjectCount(self));
    const uint32_t capacity = size + (size < count ? count : size);
    char* const storage = capacity ? static_cast<char*>(Allocate(capacity * kObjectSize)) : nullptr;
    char* out = CopyObjectsInto(self->begin, where, storage);
    for (uint32_t i = 0; i < count; ++i) {
        if (out)
            CopyObject(out, nullptr, value);
        out += kObjectSize;
    }
    if (!atEnd)
        out = CopyObjectsInto(where, self->end, out);
    DestroyObjects(self, nullptr);
    self->end = out;
    self->capacity = storage + capacity * kObjectSize;
    self->begin = storage;
    return storage;
}

void PushBackObject(ObjectVector* objects, const char* value)
{
    if (objects->end != objects->capacity) {
        if (objects->end)
            CopyObject(objects->end, nullptr, value);
        objects->end += kObjectSize;
        return;
    }
    uint8_t tag = 0;
    InsertObjects(objects, nullptr, objects->end, value, &tag, 1, 1);
}

namespace {

// 0x00479BE0: push_back (only the lot editor calls it). EAX is left as the
// copy or the new storage; nothing reads it.
void __fastcall PushBackObjectOf(ObjectVector* self, void*, const char* value)
{
    static bool logged = false;
    FirstCall("PushBackObject", logged);
    PushBackObject(self, value);
}

} // namespace

// 0x00477BB0: every object's centre moved into the rectangle (rect: the
// greatest x and z, the least x and z) less a margin of 0.04, its own
// rectangle moved with it. Whether an object was moved; false for no
// vector. The x87 code keeps the distance at 53 bits and stores floats.
uint8_t __cdecl ClampObjects(ObjectVector* objects, const float* rect)
{
    static bool logged = false;
    FirstCall("ClampObjects", logged);
    if (!objects)
        return 0;
    const double margin = Global<float>(addr::cSC4LotConfiguration_FloatEdgeMargin);
    const float leastX = static_cast<float>(static_cast<double>(rect[2]) + margin);
    const float leastZ = static_cast<float>(static_cast<double>(rect[3]) + margin);
    const float mostX = static_cast<float>(static_cast<double>(rect[0]) - margin);
    const float mostZ = static_cast<float>(static_cast<double>(rect[1]) - margin);
    uint8_t moved = 0;
    for (char* at = objects->begin; at != objects->end; at += kObjectSize) {
        bool movedX = false;
        const float x = At<float>(at, object::kX);
        if (x < leastX || x > mostX) {
            const double by = static_cast<double>(x < leastX ? leastX : mostX) - static_cast<double>(x);
            movedX = true;
            At<float>(at, object::kX) = static_cast<float>(by + static_cast<double>(At<float>(at, object::kX)));
            At<float>(at, object::kMinX) = static_cast<float>(by + static_cast<double>(At<float>(at, object::kMinX)));
            At<float>(at, object::kMaxX) = static_cast<float>(by + static_cast<double>(At<float>(at, object::kMaxX)));
        }
        const float z = At<float>(at, object::kZ);
        if (z < leastZ || z > mostZ) {
            const double by = static_cast<double>(z < leastZ ? leastZ : mostZ) - static_cast<double>(z);
            At<float>(at, object::kZ) = static_cast<float>(by + static_cast<double>(At<float>(at, object::kZ)));
            At<float>(at, object::kMinZ) = static_cast<float>(by + static_cast<double>(At<float>(at, object::kMinZ)));
            At<float>(at, object::kMaxZ) = static_cast<float>(by + static_cast<double>(At<float>(at, object::kMaxZ)));
            moved = 1;
        } else if (movedX) {
            moved = 1;
        }
    }
    return moved;
}

namespace {

// The functions game code outside the class calls directly: the manager
// (ours: lotconfig_sim.cpp), the lot editor (0x0047A780 to 0x00489650) and
// the code at 0x0049Axxx to 0x0049Cxxx and 0x0060Dxxx to 0x00610xxx. Every
// call site (0x0048034E, 0x00480376, 0x00480388, 0x004803AF, 0x004802B5,
// 0x004803E8, 0x00484423, 0x0048460E, 0x00484659, 0x004846AF, 0x004846DE,
// 0x004848C1 and its five siblings, 0x00484CAF, 0x00484E2D, 0x0048B53D,
// 0x0048BEBB, 0x0048BFF6, 0x0048C012, 0x0049AADA, 0x0049BD90, 0x0049C3F6,
// 0x0049C44D, 0x0049C465, 0x0049C4EC, 0x0049C53D, 0x0049C555, 0x0060F855,
// 0x0060F903, 0x006100B3 and the jump at 0x00A7C055) passes its arguments
// by the plain conventions and reads ECX and EDX again before using them;
// EAX is read only where the game's function returns a value, and ours
// return the same: plain detours.
Detour g_objectDetours[] = {
    {addr::cSC4LotConfiguration_CopyObject, reinterpret_cast<void*>(&CopyObject), nullptr},
    {addr::cSC4LotConfiguration_AssignObject, reinterpret_cast<void*>(&AssignObject), nullptr},
    {addr::cSC4LotConfiguration_ConstructObject, reinterpret_cast<void*>(&ConstructObject), nullptr},
    {addr::cSC4LotConfiguration_CopyObjects, reinterpret_cast<void*>(&CopyObjects), nullptr},
    {addr::cSC4LotConfiguration_CopyObjectVector, reinterpret_cast<void*>(&CopyObjectVector), nullptr},
    {addr::cSC4LotConfiguration_EraseObjects, reinterpret_cast<void*>(&EraseObjects), nullptr},
    {addr::cSC4LotConfiguration_DestroyObjects, reinterpret_cast<void*>(&DestroyObjects), nullptr},
    {addr::cSC4LotConfiguration_InsertObjects, reinterpret_cast<void*>(&InsertObjects), nullptr},
    {addr::cSC4LotConfiguration_PushBackObject, reinterpret_cast<void*>(&PushBackObjectOf), nullptr},
    {addr::cSC4LotConfiguration_ClampObjects, reinterpret_cast<void*>(&ClampObjects), nullptr},
};

bool InstallObjects(registry::Mode)
{
    return InstallDetours(g_objectDetours, sizeof(g_objectDetours) / sizeof(g_objectDetours[0]), "objects");
}

} // namespace

// --- Reset -------------------------------------------------------------------------

// 0x00479A10 (Release, the destructor and Load call it): everything but the
// reference count and the style list (+0x80) back to what the constructor
// sets, the storage kept. Always true.
uint8_t Reset(char* self)
{
    At<uint32_t>(self, field::kId) = 0;
    GameString* const name = reinterpret_cast<GameString*>(self + field::kName);
    if (name->begin != name->end) {
        *name->begin = 0;
        name->end = name->begin;
    }
    At<uint8_t>(self, field::kWidth) = 0;
    At<uint8_t>(self, field::kDepth) = 0;
    Vector32* const families = reinterpret_cast<Vector32*>(self + field::kFamilies);
    families->end = families->begin;
    At<float>(self, field::kMinSlope) = 0.0f;
    At<uint8_t>(self, field::kGrowthStage) = 1;
    At<uint16_t>(self, field::kMinCapacity) = 0xFFFF;
    At<uint16_t>(self, field::kMaxCapacity) = 0;
    At<uint8_t>(self, field::kWater) = 0;
    At<float>(self, field::kMaxSlope) = 90.0f;
    At<float>(self, field::kFoundationSlope) = 90.0f;
    BitVector* const zones = reinterpret_cast<BitVector*>(self + field::kZoneTypes);
    BitVector* const wealths = reinterpret_cast<BitVector*>(self + field::kWealthTypes);
    BitVector* const purposes = reinterpret_cast<BitVector*>(self + field::kPurposeTypes);
    for (BitVector* const bits : {zones, wealths, purposes}) {
        uint32_t result[2] = {};
        EraseBits(bits, result, bits->beginWord, bits->beginBit, bits->endWord, bits->endBit);
    }
    ResizeBits(zones, 0x10, 0);
    ResizeBits(wealths, 4, 0);
    ResizeBits(purposes, 9, 0);
    At<uint8_t>(self, field::kUnflagged) = 0;
    At<uint8_t>(self, field::kDoConstruction) = 0;
    At<uint8_t>(self, field::kRoads) = 8;
    ObjectVector* const objects = Objects(self);
    EraseObjects(objects, nullptr, objects->begin, objects->end);
    for (const std::size_t offset : {field::kRetainingWalls, field::kFences, field::kFencePositions}) {
        Vector32* const vector = reinterpret_cast<Vector32*>(self + offset);
        vector->end = vector->begin;
    }
    return 1;
}

namespace {

// --- Our object --------------------------------------------------------------------

// A copy of the game's 0x00A8CEB8 (34 slots, the deleting destructor last),
// taken at the first construction: the configurations are made at the first
// city's init, long after PostAppInit, so the other groups' slot swaps are
// in it.
void* g_vtable[kSlots];

// As the game's (0x005F47F0, shared with other classes).
uint32_t __fastcall AddRef(char* self, void*) { return ++At<uint32_t>(self, field::kRefCount); }

// As the game's (0x004777B0).
uint8_t __fastcall QueryInterface(char* self, void*, uint32_t iid, void** out)
{
    if (iid != kIidUnknown && iid != kIidConfiguration)
        return 0;
    *out = self;
    Virtual<uint32_t>(self, 0x4);
    return 1;
}

// As the game's (0x00479CE0): at the last reference Reset (called
// directly) and the deleting destructor (slot 33); the count is not
// lowered then.
uint32_t __fastcall ReleaseObject(char* self, void*)
{
    uint32_t& refs = At<uint32_t>(self, field::kRefCount);
    if (refs > 1)
        return --refs;
    Reset(self);
    Virtual<void*>(self, 0x84, 1u);
    return 0;
}

// The destructor (0x00479C20): Reset, then every container's storage in
// the game's order, the name's last.
void Destroy(char* self)
{
    At<void**>(self, 0x0) = g_vtable;
    Reset(self);
    for (const std::size_t offset : {field::kFencePositions, field::kFences, field::kRetainingWalls})
        if (void* const storage = At<void*>(self, offset))
            Free(storage);
    DestroyObjects(Objects(self), nullptr);
    for (const std::size_t offset :
         {field::kStyles, field::kPurposeTypes, field::kWealthTypes, field::kZoneTypes, field::kFamilies})
        if (void* const storage = At<void*>(self, offset))
            Free(storage);
    GameString* const name = reinterpret_cast<GameString*>(self + field::kName);
    name->vtable = addr::SC4String_Vtable;
    if (name->begin)
        Free(name->begin);
}

// Slot 33, as the game's (0x00479F50).
void* __fastcall DeletingDestructor(char* self, void*, uint32_t flags)
{
    static int destroyed = 0;
    if (destroyed++ < 3)
        log::Write("lotcfg object: destroying %p", static_cast<void*>(self));
    Destroy(self);
    if (flags & 1)
        reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_operator_delete)(self);
    return self;
}

void BuildVtable()
{
    std::memcpy(g_vtable, reinterpret_cast<const void*>(addr::cSC4LotConfiguration_vftable), sizeof(g_vtable));
    g_vtable[0] = reinterpret_cast<void*>(&QueryInterface);
    g_vtable[1] = reinterpret_cast<void*>(&AddRef);
    g_vtable[2] = reinterpret_cast<void*>(&ReleaseObject);
    g_vtable[33] = reinterpret_cast<void*>(&DeletingDestructor);
    log::Write("lotcfg object: vtable copied (slots 3/7/23/24/31/32 %p %p %p %p %p %p)", g_vtable[3], g_vtable[7],
               g_vtable[23], g_vtable[24], g_vtable[31], g_vtable[32]);
}

// The constructor (detour of 0x004797A0; every `new` of the class calls
// it: 0x00483B8D, 0x0048992F, 0x0048BDAD): the fields in the game's order,
// the name's 8 bytes from the STL allocator before the bit vectors' words.
// The padding bytes +0x3D, +0x43 and +0x8F stay as the allocation had them,
// as in the game.
void* __fastcall Construct(char* self, void*)
{
    static bool built = false;
    if (!built) {
        built = true;
        BuildVtable();
    }
    At<void**>(self, 0x0) = g_vtable;
    At<uint32_t>(self, field::kRefCount) = 0;
    At<uint32_t>(self, field::kId) = 0;
    GameString* const name = reinterpret_cast<GameString*>(self + field::kName);
    name->begin = nullptr;
    name->end = nullptr;
    name->capacity = nullptr;
    char* const text = static_cast<char*>(Allocate(8));
    name->begin = text;
    name->end = text;
    name->capacity = text + 8;
    *text = 0;
    name->vtable = addr::SC4String_Vtable;
    name->references = 0;
    At<uint8_t>(self, field::kWidth) = 0;
    At<uint8_t>(self, field::kDepth) = 0;
    std::memset(self + field::kFamilies, 0, sizeof(Vector32));
    At<float>(self, field::kMinSlope) = 0.0f;
    At<uint8_t>(self, field::kGrowthStage) = 1;
    At<uint16_t>(self, field::kMinCapacity) = 0xFFFF;
    At<uint16_t>(self, field::kMaxCapacity) = 0;
    At<uint8_t>(self, field::kWater) = 0;
    At<float>(self, field::kMaxSlope) = 90.0f;
    At<float>(self, field::kFoundationSlope) = 90.0f;
    std::memset(self + field::kZoneTypes, 0, sizeof(BitVector));
    std::memset(self + field::kWealthTypes, 0, sizeof(BitVector));
    std::memset(self + field::kPurposeTypes, 0, sizeof(BitVector));
    std::memset(self + field::kStyles, 0, sizeof(Vector32));
    At<uint8_t>(self, field::kUnflagged) = 0;
    At<uint8_t>(self, field::kDoConstruction) = 0;
    At<uint8_t>(self, field::kRoads) = 8;
    std::memset(self + field::kObjects, 0, sizeof(ObjectVector));
    std::memset(self + field::kRetainingWalls, 0, sizeof(Vector32));
    std::memset(self + field::kFences, 0, sizeof(Vector32));
    std::memset(self + field::kFencePositions, 0, sizeof(Vector32));
    ResizeBits(reinterpret_cast<BitVector*>(self + field::kZoneTypes), 0x10, 0);
    ResizeBits(reinterpret_cast<BitVector*>(self + field::kWealthTypes), 4, 0);
    ResizeBits(reinterpret_cast<BitVector*>(self + field::kPurposeTypes), 9, 0);
    static int created = 0;
    if (created++ < 3)
        log::Write("lotcfg object: constructed %p", static_cast<void*>(self));
    return self;
}

using ConstructFn = void*(__fastcall*)(char*, void*);
ConstructFn g_originalConstruct = nullptr;

bool ApplyObject(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    const addr::Entry* const entry = addr_check::Find(addr::cSC4LotConfiguration_cSC4LotConfiguration);
    return entry && hook::Install(*entry, &Construct, &g_originalConstruct);
}

const bool g_registered[] = {
    registry::Register({"simulators.lotcfg.slots", "simulators/lotcfg", registry::Kind::Vtable,
                        registry::Phase::PostAppInit, addr::cSC4LotConfiguration_vftable,
                        "Our lot configuration getters and queries: slots 3 to 18 and 20 to 30 "
                        "(src/core/lotcfg_sim.cpp)",
                        false, InstallSlots}),
    registry::Register({"simulators.lotcfg.objects", "simulators/lotcfg", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4LotConfiguration_InsertObjects,
                        "Our lot configuration object list: an object's constructor, copy and assignment, the "
                        "vector's copy, erase, insert, push_back and destructor, the objects kept inside the lot; "
                        "detours of the ten functions game code calls directly (src/core/lotcfg_sim.cpp)",
                        false, InstallObjects}),
    registry::Register({"simulators.lotcfg.object", "simulators/lotcfg", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4LotConfiguration_cSC4LotConfiguration,
                        "Our cSC4LotConfiguration object: constructor (detour), destructor, identity; methods from "
                        "a copy of the (patched) game vtable (src/core/lotcfg_sim.cpp)",
                        false, ApplyObject}),
};

} // namespace
} // namespace opensc4::lotcfg_sim
